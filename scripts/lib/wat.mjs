// Streaming lexer: parentheses inside strings/comments are not WAT structure.
// Keep only module-level spans in memory, not a syntax tree of the whole game.
export function* tokens(text) {
  let offset = 0;
  let line = 1;
  while (offset < text.length) {
    const start = offset;
    const startLine = line;
    const character = text[offset];
    if (/\s/.test(character)) {
      if (character === "\n") line++;
      offset++;
      continue;
    }
    if (text.startsWith(";;", offset)) {
      while (offset < text.length && text[offset] !== "\n") offset++;
      continue;
    }
    if (text.startsWith("(;", offset)) {
      let depth = 1;
      offset += 2;
      while (offset < text.length && depth) {
        if (text.startsWith("(;", offset)) {
          depth++;
          offset += 2;
        } else if (text.startsWith(";)", offset)) {
          depth--;
          offset += 2;
        } else {
          if (text[offset] === "\n") line++;
          offset++;
        }
      }
      if (depth) throw new Error(`Unclosed WAT comment at line ${startLine}`);
      continue;
    }
    if (character === '"') {
      offset++;
      let closed = false;
      while (offset < text.length) {
        const next = text[offset++];
        if (next === "\n") line++;
        if (next === "\\") {
          if (text[offset] === "\n") line++;
          offset++;
        } else if (next === '"') {
          closed = true;
          break;
        }
      }
      if (!closed) throw new Error(`Unclosed WAT string at line ${startLine}`);
    } else if (character === "(" || character === ")") {
      offset++;
    } else {
      while (
        offset < text.length &&
        !/[\s()"]/.test(text[offset]) &&
        !text.startsWith(";;", offset)
      )
        offset++;
    }
    yield {
      value: text.slice(start, offset),
      start,
      end: offset,
      line: startLine,
      endLine: line,
    };
  }
}

export function moduleForms(text) {
  const result = [];
  let depth = 0;
  let span;
  let rootSeen = false;
  let wantRootHead = false;
  for (const token of tokens(text)) {
    if (wantRootHead) {
      if (token.value !== "module") throw new Error("Expected a WAT module");
      wantRootHead = false;
      continue;
    }
    if (token.value === "(") {
      if (depth === 0) {
        if (rootSeen) throw new Error("Expected exactly one WAT module");
        rootSeen = true;
        wantRootHead = true;
      } else if (depth === 1) {
        span = { start: token.start, line: token.line, head: null };
      }
      depth++;
    } else if (token.value === ")") {
      depth--;
      if (depth < 0) throw new Error("Unbalanced WAT parentheses");
      if (depth === 1)
        result.push({ ...span, end: token.end, endLine: token.endLine });
    } else if (depth === 2 && span.head === null) {
      span.head = token.value;
    } else if (depth === 0) {
      throw new Error("Unexpected text outside WAT module");
    }
  }
  if (!rootSeen || depth !== 0 || wantRootHead)
    throw new Error("Incomplete WAT module");
  return result;
}

function smallExpression(text) {
  const stack = [];
  let result;
  for (const token of tokens(text)) {
    if (token.value === "(") {
      const node = [];
      if (stack.length) stack.at(-1).push(node);
      else result = node;
      stack.push(node);
    } else if (token.value === ")") stack.pop();
    else stack.at(-1).push(token.value);
  }
  return result;
}

function header(text) {
  const iterator = tokens(text);
  if (
    iterator.next().value?.value !== "(" ||
    iterator.next().value?.value !== "func"
  ) {
    throw new Error("Expected a function definition");
  }
  const id = iterator.next().value?.value;
  if (!id?.startsWith("$"))
    throw new Error("Functions must have WAT identifiers");
  const groups = [];
  let token = iterator.next().value;
  while (token?.value === "(") {
    const head = iterator.next().value;
    if (!["type", "param", "result", "local"].includes(head?.value)) break;
    let depth = 1;
    let end;
    while (depth) {
      end = iterator.next().value;
      if (!end) throw new Error(`Incomplete header: ${id}`);
      if (end.value === "(") depth++;
      if (end.value === ")") depth--;
    }
    groups.push(smallExpression(text.slice(token.start, end.end)));
    token = iterator.next().value;
  }
  return { id, groups };
}

function signature(groups, types) {
  const type = groups.find((group) => group[0] === "type")?.[1];
  const inherited = type ? types.get(type) : null;
  if (type && !inherited) throw new Error(`Unknown WAT type: ${type}`);
  const typeName = (value) =>
    Array.isArray(value) ? `(${value.map(typeName).join(" ")})` : value;
  function values(kind) {
    return groups
      .filter((group) => group[0] === kind)
      .flatMap((group) => group.slice(1))
      .filter((value) => typeof value !== "string" || !value.startsWith("$"))
      .map(typeName);
  }
  const params = values("param");
  const results = values("result");
  return {
    params: params.length ? params : (inherited?.params ?? []),
    results: results.length ? results : (inherited?.results ?? []),
  };
}

function stringValue(value) {
  const escapes = { n: "\n", r: "\r", t: "\t", '"': '"', "'": "'", "\\": "\\" };
  const parts = value.slice(1, -1).split(/(\\(?:[0-9a-fA-F]{2}|.))/g);
  return Buffer.concat(
    parts.map((part) => {
      if (/^\\[0-9a-fA-F]{2}$/.test(part))
        return Buffer.from([parseInt(part.slice(1), 16)]);
      if (part.startsWith("\\"))
        return Buffer.from(escapes[part.slice(1)] ?? part.slice(1));
      return Buffer.from(part);
    }),
  ).toString("utf8");
}

export function analyzeWat(text, { includeCalls = true } = {}) {
  const forms = moduleForms(text);
  const types = new Map();
  let typeIndex = 0;
  for (const form of forms.filter((item) => item.head === "type")) {
    const expression = smallExpression(text.slice(form.start, form.end));
    const id = typeof expression[1] === "string" ? expression[1] : null;
    const body = expression.find(
      (item) => Array.isArray(item) && item[0] === "func",
    );
    if (!body)
      throw new Error("Function analysis requires ordinary function types");
    const value = signature(body.slice(1), types);
    types.set(String(typeIndex++), value);
    if (id) types.set(id, value);
  }
  const functions = [];
  for (const form of forms.filter((item) => item.head === "import")) {
    const expression = smallExpression(text.slice(form.start, form.end));
    const body = expression[3];
    if (body?.[0] !== "func") continue;
    const id = body[1];
    if (typeof id !== "string" || !id.startsWith("$"))
      throw new Error("Imported functions must be named");
    functions.push({
      index: functions.length,
      id,
      imported: true,
      importModule: stringValue(expression[1]),
      importName: stringValue(expression[2]),
      signature: signature(body.slice(2), types),
      ...form,
      calls: [],
      referencedFunctions: [],
      indirectCalls: 0,
      exports: [],
    });
  }
  for (const form of forms.filter((item) => item.head === "func")) {
    const body = text.slice(form.start, form.end);
    const { id, groups } = header(body);
    const calls = new Set();
    const references = new Set();
    let indirectCalls = 0;
    let instruction;
    for (const token of includeCalls ? tokens(body) : []) {
      if (instruction === "call" || instruction === "return_call")
        calls.add(token.value);
      if (instruction === "ref.func") references.add(token.value);
      instruction = token.value;
      if (
        instruction === "call_indirect" ||
        instruction === "return_call_indirect"
      )
        indirectCalls++;
    }
    functions.push({
      index: functions.length,
      id,
      imported: false,
      signature: signature(groups, types),
      ...form,
      calls: [...calls],
      referencedFunctions: [...references],
      indirectCalls,
      exports: [],
    });
  }
  const byId = new Map(functions.map((item) => [item.id, item]));
  if (byId.size !== functions.length)
    throw new Error("Duplicate function identifiers");
  const resolve = (reference) => {
    const target = reference.startsWith("$")
      ? byId.get(reference)
      : functions[Number(reference)];
    if (!target) throw new Error(`Unknown function reference: ${reference}`);
    return target;
  };
  for (const form of forms.filter((item) => item.head === "export")) {
    const expression = smallExpression(text.slice(form.start, form.end));
    if (expression[2]?.[0] === "func")
      resolve(expression[2][1]).exports.push(stringValue(expression[1]));
  }
  for (const item of functions) {
    item.calls = [
      ...new Set(item.calls.map((reference) => resolve(reference).id)),
    ];
    item.referencedFunctions = [
      ...new Set(
        item.referencedFunctions.map((reference) => resolve(reference).id),
      ),
    ];
    item.callers = [];
  }
  for (const item of functions) {
    for (const callee of item.calls) byId.get(callee).callers.push(item.id);
  }
  return functions;
}

export const formatSignature = ({ params, results }) =>
  `(${params.join(", ")}) -> ${results.length ? results.join(", ") : "void"}`;

export function externalFunctionABI(text) {
  const functions = analyzeWat(text, { includeCalls: false });
  return {
    imports: functions
      .filter((item) => item.imported)
      .map((item) => ({
        module: item.importModule,
        name: item.importName,
        signature: item.signature,
      })),
    exports: functions
      .flatMap((item) =>
        item.exports.map((name) => ({
          name,
          signature: item.signature,
        })),
      )
      .sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0)),
  };
}

export function assertFunctionABI(expected, actual) {
  for (const kind of ["imports", "exports"]) {
    if (JSON.stringify(expected[kind]) !== JSON.stringify(actual[kind])) {
      throw new Error(
        `WASM ${kind} function signatures changed; keep the loader's parameter and result types intact.`,
      );
    }
  }
}

// Decompiler emits one header per defined function, with its absolute index.
// Keep each slice as emitted; don't mistake it for compilable source.
export function pseudocodeFunctions(text) {
  const headers = [
    ...text.matchAll(/^(?:export )?function [^\n]+\{ \/\/ func(\d+)\r?$/gm),
  ];
  return new Map(
    headers.map((match, index) => [
      Number(match[1]),
      text
        .slice(match.index, headers[index + 1]?.index ?? text.length)
        .trimEnd() + "\n",
    ]),
  );
}
