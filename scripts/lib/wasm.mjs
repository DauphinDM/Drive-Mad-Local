function readULEB(bytes, position, limit) {
  let value = 0;
  let factor = 1;
  for (let count = 0; count < 5 && position < limit; count++) {
    const byte = bytes[position++];
    value += (byte & 0x7f) * factor;
    if (value > 0xffffffff) throw new Error("WASM integer exceeds u32");
    if (!(byte & 0x80)) return { value, position };
    factor *= 128;
  }
  throw new Error("Invalid WASM unsigned LEB128 integer");
}

function uleb(value) {
  const bytes = [];
  do {
    const next = value % 128;
    value = Math.floor(value / 128);
    bytes.push(next | (value ? 0x80 : 0));
  } while (value);
  return Buffer.from(bytes);
}

// WABT 1.0.39's decompiler mishandles named parameters: it declares p0 as a
// separate local while the parameter is called "a". Remove only local debug
// names from the analysis copy. Function names and all executable bytes remain.
export function stripLocalDebugNames(binary) {
  const bytes = Buffer.from(binary);
  const header = Buffer.from([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0]);
  if (!bytes.subarray(0, 8).equals(header))
    throw new Error("Expected a WASM v1 binary");
  const parts = [bytes.subarray(0, 8)];
  let position = 8;
  while (position < bytes.length) {
    const start = position;
    const id = bytes[position++];
    const length = readULEB(bytes, position, bytes.length);
    const end = length.position + length.value;
    if (end > bytes.length) throw new Error("Truncated WASM section");
    if (id !== 0) {
      parts.push(bytes.subarray(start, end));
      position = end;
      continue;
    }
    const name = readULEB(bytes, length.position, end);
    const nameEnd = name.position + name.value;
    if (nameEnd > end) throw new Error("Truncated WASM custom section name");
    if (bytes.subarray(name.position, nameEnd).toString("utf8") !== "name") {
      parts.push(bytes.subarray(start, end));
      position = end;
      continue;
    }
    const subsections = [bytes.subarray(length.position, nameEnd)];
    let current = nameEnd;
    let changed = false;
    while (current < end) {
      const subStart = current;
      const subId = bytes[current++];
      const subLength = readULEB(bytes, current, end);
      current = subLength.position + subLength.value;
      if (current > end) throw new Error("Truncated WASM name subsection");
      if (subId === 2) changed = true;
      else subsections.push(bytes.subarray(subStart, current));
    }
    if (changed) {
      const payload = Buffer.concat(subsections);
      parts.push(Buffer.from([0]), uleb(payload.length), payload);
    } else parts.push(bytes.subarray(start, end));
    position = end;
  }
  return Buffer.concat(parts);
}
