/**
 * Struct marshalling, driven by the schema the wasm module reports.
 *
 * The C compiler owns the layout of es_engine_def and friends. Rather than
 * copy that layout here and watch the two drift, the module describes itself
 * at startup and this walks the description. Add a field to enginesim.h and it
 * appears on both sides at once.
 *
 * Everything written goes into one arena, so a definition, however deeply
 * nested, is released with a single call.
 */
import type { EngineModule, Ptr } from '../wasm/enginesim.mjs';

export type FieldKind = 'f64' | 'u32' | 'i32' | 'str' | 'sub' | 'arr' | 'arrf' | 'arru' | 'arri16';

export interface Field {
  name: string;
  offset: number;
  kind: FieldKind;
  args: string[];
}

export interface Struct {
  size: number;
  fields: Field[];
}

export type Schema = Record<string, Struct>;

/** Anything the marshaller will accept as a struct. */
type Plain = Record<string, unknown>;

/** Parses the schema string into `{ structName: { size, fields } }`. */
export function parseSchema(text: string): Schema {
  const structs: Schema = {};
  for (const line of text.split('\n')) {
    const [head = '', ...rest] = line.split('|');
    const [name = '', size = '0'] = head.split(':');
    structs[name] = {
      size: Number(size),
      fields: rest.map((f) => {
        const [fname = '', offset = '0', kind = 'f64', ...args] = f.split(':');
        return { name: fname, offset: Number(offset), kind: kind as FieldKind, args };
      }),
    };
  }
  return structs;
}

/**
 * A scratch region in the wasm heap. Everything allocated through it is freed
 * together, which is what makes nested definitions manageable.
 */
export class Arena {
  readonly #module: EngineModule;
  readonly #blocks: Ptr[] = [];

  constructor(module: EngineModule) {
    this.#module = module;
  }

  alloc(bytes: number): Ptr {
    const ptr = this.#module._es_js_alloc(bytes);
    if (!ptr) throw new Error(`out of memory allocating ${bytes} bytes`);
    this.#blocks.push(ptr);
    return ptr;
  }

  free(): void {
    for (const p of this.#blocks) this.#module._es_js_free(p);
    this.#blocks.length = 0;
  }

  f64(values: readonly number[]): Ptr {
    const ptr = this.alloc(Math.max(1, values.length) * 8);
    this.#module.HEAPF64.set(Float64Array.from(values), ptr >> 3);
    return ptr;
  }

  u32(values: readonly number[]): Ptr {
    const ptr = this.alloc(Math.max(1, values.length) * 4);
    this.#module.HEAPU32.set(Uint32Array.from(values), ptr >> 2);
    return ptr;
  }

  i16(values: readonly number[]): Ptr {
    const ptr = this.alloc(Math.max(1, values.length) * 2);
    new Int16Array(this.#module.HEAPU8.buffer, ptr, values.length).set(Int16Array.from(values));
    return ptr;
  }

  str(text: string): Ptr {
    /* Emscripten's own writer, not TextEncoder: an AudioWorkletGlobalScope
     * has no TextEncoder. Four bytes per code unit is the worst UTF-8 can do,
     * plus the terminator. */
    const bytes = text.length * 4 + 1;
    const ptr = this.alloc(bytes);
    this.#module.stringToUTF8(text, ptr, bytes);
    return ptr;
  }
}

/** Total cylinders across every bank; resolves the "@cylinders" count. */
export function cylinderCount(engine: Plain | undefined): number {
  const banks = (engine?.['banks'] ?? []) as Plain[];
  return banks.reduce((n, b) => n + ((b?.['cylinders'] as unknown[])?.length ?? 0), 0);
}

/**
 * Writes a plain object into a struct at `ptr`.
 *
 * Missing fields keep the zero the arena already wrote, which is what the C
 * side documents as "select the default" nearly everywhere.
 */
export function writeStruct(
  M: EngineModule,
  schema: Schema,
  arena: Arena,
  structName: string,
  ptr: Ptr,
  value: Plain | undefined,
): void {
  const def = schema[structName];
  if (!def) throw new Error(`unknown struct "${structName}"`);

  for (const field of def.fields) {
    const v = value?.[field.name];
    const at = ptr + field.offset;

    switch (field.kind) {
      case 'f64':
        if (v !== undefined) M.HEAPF64[at >> 3] = numeric(v, structName, field.name);
        break;

      case 'u32':
      case 'i32': {
        /* Count fields follow the arrays they describe, so a caller never has
         * to keep the two in step by hand. */
        const derived = derivedCount(def, field.name, value);
        const n = derived ?? v;
        if (n !== undefined) {
          M.HEAPU32[at >> 2] = Math.round(numeric(n, structName, field.name)) >>> 0;
        }
        break;
      }

      case 'str':
        if (typeof v === 'string') M.HEAPU32[at >> 2] = arena.str(v);
        break;

      case 'sub':
        if (v !== undefined) {
          writeStruct(M, schema, arena, expect(field.args[0]), at, v as Plain);
        }
        break;

      case 'arrf':
        if (v !== undefined) M.HEAPU32[at >> 2] = arena.f64(asNumbers(v, structName, field.name));
        break;

      case 'arru':
        if (v !== undefined) M.HEAPU32[at >> 2] = arena.u32(asNumbers(v, structName, field.name));
        break;

      case 'arri16':
        if (v !== undefined) M.HEAPU32[at >> 2] = arena.i16(asNumbers(v, structName, field.name));
        break;

      case 'arr': {
        if (v === undefined) break;
        const items = asArray(v, structName, field.name) as Plain[];
        const elemName = expect(field.args[0]);
        const elem = schema[elemName];
        if (!elem) throw new Error(`unknown struct "${elemName}"`);
        const block = arena.alloc(Math.max(1, items.length) * elem.size);
        items.forEach((item, i) =>
          writeStruct(M, schema, arena, elemName, block + i * elem.size, item));
        M.HEAPU32[at >> 2] = block;
        break;
      }
    }
  }
}

/**
 * Reads a struct back out into a plain object.
 *
 * This is what lets a host load a built-in preset and show its actual numbers
 * rather than keeping a second copy of them in JavaScript.
 */
export function readStruct(
  M: EngineModule,
  schema: Schema,
  structName: string,
  ptr: Ptr,
): Plain {
  const def = schema[structName];
  if (!def) throw new Error(`unknown struct "${structName}"`);

  const out: Plain = {};
  const counts: Record<string, number> = {};
  for (const f of def.fields) {
    if (f.kind === 'u32' || f.kind === 'i32') counts[f.name] = M.HEAPU32[(ptr + f.offset) >> 2] ?? 0;
  }

  for (const field of def.fields) {
    const at = ptr + field.offset;
    const p = M.HEAPU32[at >> 2] ?? 0;

    switch (field.kind) {
      case 'f64': out[field.name] = M.HEAPF64[at >> 3] ?? 0; break;
      case 'u32': out[field.name] = counts[field.name] ?? 0; break;
      case 'i32': out[field.name] = (counts[field.name] ?? 0) | 0; break;
      case 'str': out[field.name] = p ? M.UTF8ToString(p) : null; break;
      case 'sub': out[field.name] = readStruct(M, schema, expect(field.args[0]), at); break;

      case 'arrf': {
        const n = resolveCount(field.args[0], counts, out);
        out[field.name] = p && n ? Array.from(M.HEAPF64.subarray(p >> 3, (p >> 3) + n)) : [];
        break;
      }
      case 'arru': {
        const n = resolveCount(field.args[0], counts, out);
        out[field.name] = p && n ? Array.from(M.HEAPU32.subarray(p >> 2, (p >> 2) + n)) : [];
        break;
      }
      case 'arri16': {
        const n = resolveCount(field.args[0], counts, out);
        out[field.name] = p && n ? Array.from(new Int16Array(M.HEAPU8.buffer, p, n)) : [];
        break;
      }
      case 'arr': {
        const n = resolveCount(field.args[1], counts, out);
        const elemName = expect(field.args[0]);
        const elem = schema[elemName];
        if (!elem) throw new Error(`unknown struct "${elemName}"`);
        const items: Plain[] = [];
        for (let i = 0; i < (p ? n : 0); ++i) {
          items.push(readStruct(M, schema, elemName, p + i * elem.size));
        }
        out[field.name] = items;
        break;
      }
    }
  }

  /* Counts are implied by array length once this is an object again. */
  for (const f of def.fields) {
    if (f.kind !== 'u32') continue;
    if (def.fields.some((o) => o.kind !== 'u32' && o.args.includes(f.name))) delete out[f.name];
  }
  return out;
}

function resolveCount(
  token: string | undefined,
  counts: Record<string, number>,
  partial: Plain,
): number {
  if (token === '@cylinders') return cylinderCount(partial);
  return (token ? counts[token] : 0) ?? 0;
}

/** A count field's value, taken from the array it describes. */
function derivedCount(def: Struct, countName: string, value: Plain | undefined): number | undefined {
  const owner = def.fields.find((f) => f.kind !== 'u32' && f.args.includes(countName));
  if (!owner) return undefined;
  const arr = value?.[owner.name];
  return Array.isArray(arr) ? arr.length : undefined;
}

function expect(name: string | undefined): string {
  if (!name) throw new Error('schema field is missing its struct argument');
  return name;
}

function asArray(v: unknown, structName: string, field: string): unknown[] {
  if (Array.isArray(v)) return v;
  if (ArrayBuffer.isView(v)) return Array.from(v as unknown as ArrayLike<unknown>);
  throw new TypeError(`${structName}.${field} must be an array, got ${typeof v}`);
}

function asNumbers(v: unknown, structName: string, field: string): number[] {
  return asArray(v, structName, field).map((x) => numeric(x, structName, field));
}

function numeric(v: unknown, structName: string, field: string): number {
  const n = Number(v);
  if (!Number.isFinite(n)) {
    throw new TypeError(
      `${structName}.${field} must be a finite number, got ${JSON.stringify(v)}`);
  }
  return n;
}
