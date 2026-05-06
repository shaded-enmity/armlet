export interface BitlayoutMember {
  kind: "named" | "immediate";
  size: number;
  msb: number;
  lsb: number;
  name?: string;
  value?: string;
}

export interface BitlayoutSpec {
  name: string;
  total: number;
  members: BitlayoutMember[];
}

function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  cls?: string,
  text?: string
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  if (cls) node.className = cls;
  if (text !== undefined) node.textContent = text;
  return node;
}

function validate(spec: unknown): asserts spec is BitlayoutSpec {
  if (!spec || typeof spec !== "object") {
    throw new Error("Expected a JSON object at the top level");
  }
  const s = spec as Record<string, unknown>;
  if (typeof s.name !== "string") throw new Error("'name' must be a string");
  if (typeof s.total !== "number" || !Number.isInteger(s.total) || s.total <= 0) {
    throw new Error("'total' must be a positive integer");
  }
  if (!Array.isArray(s.members)) throw new Error("'members' must be an array");
  s.members.forEach((m, i) => {
    if (!m || typeof m !== "object") {
      throw new Error(`members[${i}] must be an object`);
    }
    const mm = m as Record<string, unknown>;
    if (mm.kind !== "named" && mm.kind !== "immediate") {
      throw new Error(`members[${i}].kind must be "named" or "immediate"`);
    }
    for (const k of ["size", "msb", "lsb"]) {
      if (typeof mm[k] !== "number" || !Number.isInteger(mm[k] as number)) {
        throw new Error(`members[${i}].${k} must be an integer`);
      }
    }
    if (mm.kind === "named" && typeof mm.name !== "string") {
      throw new Error(`members[${i}].name must be a string`);
    }
    if (mm.kind === "immediate" && typeof mm.value !== "string") {
      throw new Error(`members[${i}].value must be a string`);
    }
  });
}

export function renderBitlayout(spec: BitlayoutSpec, container: HTMLElement): void {
  validate(spec);

  container.replaceChildren();

  const root = el("div", "bitlayout");
  root.style.setProperty("--total-bits", String(spec.total));

  const title = el("div", "bitlayout__title");
  const tag = el("span");
  tag.textContent = `bitlayout (${spec.total} bits)`;
  const name = el("strong");
  name.textContent = spec.name;
  title.append(tag, name);
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
