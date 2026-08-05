/*
 * Generates src/lib/types.ts from the schema the wasm module reports.
 *
 * The C header owns the shape of an engine definition. Hand-writing the
 * TypeScript for it would put a second copy of that shape in the repository,
 * free to drift; deriving it means adding a field to enginesim.h gives you the
 * type as well, and removing one turns every stale use into a compile error.
 *
 * Run by the pre* scripts in package.json, so it is never out of date in
 * practice: node scripts/generate-types.mjs
 */
import { readFileSync, writeFileSync } from 'node:fs';

const wasmDir = new URL('../wasm/', import.meta.url);
const { default: createEngineSim } = await import(new URL('enginesim.mjs', wasmDir));
const M = await createEngineSim({ wasmBinary: readFileSync(new URL('enginesim.wasm', wasmDir)) });

const structs = M.UTF8ToString(M._es_js_schema()).split('\n').map((line) => {
  const [head, ...rest] = line.split('|');
  const [name] = head.split(':');
  return {
    name,
    fields: rest.map((f) => {
      const [fname, , kind, ...args] = f.split(':');
      return { name: fname, kind, args };
    }),
  };
});

/** cylinder_def -> CylinderDef */
const typeName = (s) => s.split('_').map((w) => w[0].toUpperCase() + w.slice(1)).join('');

/** Fields that another field names as its count are derived from that array. */
function derivedCounts(fields) {
  const set = new Set();
  for (const f of fields) {
    for (const a of f.args) if (fields.some((o) => o.name === a)) set.add(a);
  }
  return set;
}

function tsType(field) {
  switch (field.kind) {
    case 'f64': case 'u32': case 'i32': return 'number';
    case 'str': return 'string';
    case 'sub': return typeName(field.args[0]);
    case 'arr': return `${typeName(field.args[0])}[]`;
    case 'arrf': case 'arru': case 'arri16': return 'number[]';
    default: throw new Error(`unhandled kind ${field.kind}`);
  }
}

let out = `/*
 * Generated from the C struct layout by scripts/generate-types.mjs.
 * Do not edit: change core/include/enginesim.h and regenerate.
 *
 * Every field is optional. The ABI reads a zeroed struct as "use the default"
 * almost everywhere, so an engine definition only has to say what it means to
 * change. Count fields are absent on purpose - they are derived from the
 * length of the array they describe when the definition is marshalled.
 */

`;

for (const struct of structs) {
  const counts = derivedCounts(struct.fields);
  const readonlyStruct = struct.name === 'telemetry';
  out += `export interface ${typeName(struct.name)} {\n`;
  for (const field of struct.fields) {
    if (counts.has(field.name) && !readonlyStruct) continue;
    out += `  ${field.name}${readonlyStruct ? '' : '?'}: ${tsType(field)};\n`;
  }
  out += '}\n\n';
}

out += `/** One of the engines built into the library. */
export interface Preset {
  index: number;
  name: string;
  def: EngineDef;
}
`;

const target = new URL('../src/types.ts', import.meta.url);
writeFileSync(target, out);
console.log(`generated ${structs.length} interfaces -> src/lib/types.ts`);
