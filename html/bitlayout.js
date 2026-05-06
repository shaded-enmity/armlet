function el(tag, cls, text) {
  const node = document.createElement(tag);
  if (cls) node.className = cls;
  if (text !== undefined) node.textContent = text;
  return node;
}

function validate(spec) {
  if (!spec || typeof spec !== "object") {
    throw new Error("Expected a JSON object at the top level");
  }
  if (typeof spec.name !== "string") throw new Error("'name' must be a string");
  if (typeof spec.total !== "number" || !Number.isInteger(spec.total) || spec.total <= 0) {
    throw new Error("'total' must be a positive integer");
  }
  if (!Array.isArray(spec.members)) throw new Error("'members' must be an array");
  spec.members.forEach((m, i) => {
    if (!m || typeof m !== "object") {
      throw new Error(`members[${i}] must be an object`);
    }
    if (m.kind !== "named" && m.kind !== "immediate") {
      throw new Error(`members[${i}].kind must be "named" or "immediate"`);
    }
    for (const k of ["size", "msb", "lsb"]) {
      if (typeof m[k] !== "number" || !Number.isInteger(m[k])) {
        throw new Error(`members[${i}].${k} must be an integer`);
      }
    }
    if (m.kind === "named" && typeof m.name !== "string") {
      throw new Error(`members[${i}].name must be a string`);
    }
    if (m.kind === "immediate" && typeof m.value !== "string") {
      throw new Error(`members[${i}].value must be a string`);
    }
  });
}

export function renderBitlayout(spec, container) {
  validate(spec);

  container.replaceChildren();

  const root = el("div", "bitlayout");
  root.style.setProperty("--total-bits", String(spec.total));

  const title = el("div", "bitlayout__title");
  const tag = el("span");
  tag.textContent = ` (${spec.total} bits)`;
  const name = el("strong");
  name.textContent = spec.name;
  title.append(name, tag);
  root.appendChild(title);

  const indexRow = el("div", "bitlayout__row bitlayout__row--index");
  indexRow.style.display = "contents";
  const valueRow = el("div", "bitlayout__row bitlayout__row--values");
  valueRow.style.display = "contents";

  for (const m of spec.members) {
    const idxCell = el("div", "bitlayout__cell");
    idxCell.style.setProperty("--size", String(m.size));
    if (m.size === 1) {
      idxCell.classList.add("is-single");
      idxCell.appendChild(el("span", "bitlayout__index-msb", String(m.msb)));
    } else {
      idxCell.appendChild(el("span", "bitlayout__index-msb", String(m.msb)));
      idxCell.appendChild(el("span", "bitlayout__index-lsb", String(m.lsb)));
    }
    indexRow.appendChild(idxCell);

    const valCell = el("div", "bitlayout__cell");
    valCell.style.setProperty("--size", String(m.size));
    if (m.kind === "named") {
      valCell.classList.add("bitlayout__cell--named");
      valCell.textContent = m.name ?? "";
    } else {
      valCell.classList.add("bitlayout__cell--immediate");
      const bits = m.value ?? "";
      for (const ch of bits) {
        valCell.appendChild(el("span", "bitlayout__bit", ch));
      }
    }
    valueRow.appendChild(valCell);
  }

  root.append(indexRow, valueRow);
  container.appendChild(root);
}
