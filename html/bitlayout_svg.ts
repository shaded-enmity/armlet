import type { BitlayoutSpec, BitlayoutMember } from "./bitlayout.js";

export type SvgStyle = "clean" | "xkcd" | "whimsical" | "crayon";

export interface SvgRenderOptions {
  style?: SvgStyle;
  bitWidth?: number;
}

const SVG_NS = "http://www.w3.org/2000/svg";

function mulberry32(seed: number): () => number {
  return function () {
    let t = (seed += 0x6d2b79f5);
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function hashSeed(s: string): number {
  let h = 2166136261;
  for (let i = 0; i < s.length; i++) {
    h ^= s.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return h >>> 0;
}

function n(tag: string, attrs?: Record<string, string | number>, text?: string): SVGElement {
  const el = document.createElementNS(SVG_NS, tag) as SVGElement;
  if (attrs) {
    for (const [k, v] of Object.entries(attrs)) {
      el.setAttribute(k, String(v));
    }
  }
  if (text !== undefined) el.textContent = text;
  return el;
}

interface StyleSpec {
  bg: string;
  panel: string;
  stroke: string;
  strokeWidth: number;
  text: string;
  named: string;
  immediate: string;
  index: string;
  font: string;
  fontWeight: string;
  wobbleAmp: number;
  cornerRadius: number;
  cellFill: (m: BitlayoutMember, idx: number) => string;
  cellExtras: (g: SVGElement, m: BitlayoutMember, x: number, y: number, w: number, h: number, rng: () => number) => void;
  filter?: string;
}

const PASTELS = ["#ffe1e9", "#e1f4ff", "#fff4d6", "#e6ffe1", "#f0e1ff", "#ffe9d6", "#d6f7ff", "#fde6f5"];
const CRAYONS = ["#ff5c8a", "#ffaa3a", "#ffe24a", "#7ed957", "#3ec1ff", "#a37dff", "#ff7e3e", "#48d4b1"];

function styleSpec(style: SvgStyle): StyleSpec {
  switch (style) {
    case "xkcd":
      return {
        bg: "#fdfdfa",
        panel: "#ffffff",
        stroke: "#1a1a1a",
        strokeWidth: 1.6,
        text: "#1a1a1a",
        named: "#1a1a1a",
        immediate: "#1a1a1a",
        index: "#3a3a3a",
        font: '"Patrick Hand", "Comic Neue", "Comic Sans MS", "Bradley Hand", cursive',
        fontWeight: "400",
        wobbleAmp: 1.4,
        cornerRadius: 0,
        cellFill: () => "none",
        cellExtras: () => {},
      };
    case "whimsical":
      return {
        bg: "#fff8fb",
        panel: "#ffffff",
        stroke: "#7a4d8a",
        strokeWidth: 1.4,
        text: "#4a2a55",
        named: "#7a4d8a",
        immediate: "#c44a7a",
        index: "#9a7aa8",
        font: '"Caveat", "Patrick Hand", "Comic Sans MS", cursive',
        fontWeight: "500",
        wobbleAmp: 0.4,
        cornerRadius: 8,
        cellFill: (m, i) =>
          m.kind === "named" ? PASTELS[i % PASTELS.length] : "#ffffff",
        cellExtras: () => {},
        filter: "whimsical-shadow",
      };
    case "crayon":
      return {
        bg: "#faf6e8",
        panel: "#fffaea",
        stroke: "#2a1a08",
        strokeWidth: 2.6,
        text: "#2a1a08",
        named: "#2a1a08",
        immediate: "#2a1a08",
        index: "#3a2a14",
        font: '"Caveat", "Patrick Hand", "Comic Sans MS", cursive',
        fontWeight: "700",
        wobbleAmp: 2.0,
        cornerRadius: 0,
        cellFill: (m, i) =>
          m.kind === "named" ? CRAYONS[i % CRAYONS.length] : "transparent",
        cellExtras: (g, m, x, y, w, h, rng) => {
          if (m.kind !== "named") return;
          const hatchN = Math.max(3, Math.round(w / 8));
          for (let i = 0; i < hatchN; i++) {
            const t = (i + 0.5) / hatchN;
            const sx = x + w * t + (rng() - 0.5) * 2;
            g.appendChild(
              n("line", {
                x1: sx,
                y1: y + 2,
                x2: sx + (rng() - 0.5) * 4,
                y2: y + h - 2,
                stroke: "rgba(0,0,0,0.18)",
                "stroke-width": 1.2 + rng() * 0.8,
                "stroke-linecap": "round",
              }),
            );
          }
        },
        filter: "crayon-warp",
      };
    case "clean":
    default:
      return {
        bg: "#0e1a2b",
        panel: "#0e1a2b",
        stroke: "#7fc8d8",
        strokeWidth: 1,
        text: "#b9d8e6",
        named: "#a4f0ff",
        immediate: "#e8d27a",
        index: "#5e8aa6",
        font: '"JetBrains Mono", "Menlo", monospace',
        fontWeight: "500",
        wobbleAmp: 0,
        cornerRadius: 0,
        cellFill: () => "none",
        cellExtras: () => {},
      };
  }
}

function wobblePath(
  pts: Array<[number, number]>,
  rng: () => number,
  amp: number,
  closed = false,
): string {
  if (amp === 0) {
    let d = `M ${pts[0][0].toFixed(2)} ${pts[0][1].toFixed(2)}`;
    for (let i = 1; i < pts.length; i++) {
      d += ` L ${pts[i][0].toFixed(2)} ${pts[i][1].toFixed(2)}`;
    }
    if (closed) d += " Z";
    return d;
  }
  let d = "";
  for (let i = 0; i < pts.length - 1; i++) {
    const [x1, y1] = pts[i];
    const [x2, y2] = pts[i + 1];
    const dx = x2 - x1,
      dy = y2 - y1;
    const len = Math.hypot(dx, dy) || 1;
    const segs = Math.max(2, Math.round(len / 16));
    const nx = -dy / len,
      ny = dx / len;
    if (i === 0) d += `M ${x1.toFixed(2)} ${y1.toFixed(2)}`;
    for (let j = 1; j <= segs; j++) {
      const t = j / segs;
      const tx = x1 + dx * t;
      const ty = y1 + dy * t;
      const isEnd = j === segs && i === pts.length - 2 && !closed;
      const off = isEnd ? 0 : (rng() - 0.5) * 2 * amp;
      d += ` L ${(tx + nx * off).toFixed(2)} ${(ty + ny * off).toFixed(2)}`;
    }
  }
  if (closed) d += " Z";
  return d;
}

function rectPath(x: number, y: number, w: number, h: number, rng: () => number, amp: number): string {
  return wobblePath(
    [
      [x, y],
      [x + w, y],
      [x + w, y + h],
      [x, y + h],
      [x, y],
    ],
    rng,
    amp,
    true,
  );
}

function defs(spec: StyleSpec): SVGElement {
  const d = n("defs");
  if (spec.filter === "crayon-warp") {
    const f = n("filter", { id: "crayon-warp", x: "-5%", y: "-5%", width: "110%", height: "110%" });
    f.appendChild(n("feTurbulence", { type: "fractalNoise", baseFrequency: "0.85", numOctaves: "2", seed: "7" }));
    f.appendChild(n("feDisplacementMap", { in: "SourceGraphic", scale: "1.3" }));
    d.appendChild(f);
  }
  if (spec.filter === "whimsical-shadow") {
    const f = n("filter", { id: "whimsical-shadow", x: "-10%", y: "-10%", width: "120%", height: "130%" });
    f.appendChild(n("feGaussianBlur", { in: "SourceAlpha", stdDeviation: "1.6" }));
    f.appendChild(n("feOffset", { dx: "1", dy: "2", result: "off" }));
    f.appendChild(n("feComponentTransfer", { in: "off" })).appendChild(
      n("feFuncA", { type: "linear", slope: "0.35" }),
    );
    const merge = n("feMerge");
    merge.appendChild(n("feMergeNode"));
    merge.appendChild(n("feMergeNode", { in: "SourceGraphic" }));
    f.appendChild(merge);
    d.appendChild(f);
  }
  return d;
}

function validate(spec: unknown): asserts spec is BitlayoutSpec {
  if (!spec || typeof spec !== "object") throw new Error("Expected JSON object");
  const s = spec as Record<string, unknown>;
  if (typeof s.name !== "string") throw new Error("'name' must be string");
  if (typeof s.total !== "number" || !Number.isInteger(s.total) || s.total <= 0)
    throw new Error("'total' must be positive integer");
  if (!Array.isArray(s.members)) throw new Error("'members' must be array");
}

export function renderBitlayoutSVG(
  spec: BitlayoutSpec,
  container: HTMLElement,
  options: SvgRenderOptions = {},
): void {
  validate(spec);
  const style = options.style ?? "clean";
  const ss = styleSpec(style);
  const bw = options.bitWidth ?? 22;

  const padX = 16;
  const padY = 12;
  const titleH = 30;
  const indexH = 26;
  const valueH = 44;
  const innerW = spec.total * bw;
  const W = innerW + padX * 2;
  const H = titleH + indexH + valueH + padY * 2;

  const rng = mulberry32(hashSeed(spec.name + ":" + style + ":" + spec.total));

  container.replaceChildren();
  const svg = n("svg", {
    xmlns: SVG_NS,
    viewBox: `0 0 ${W} ${H}`,
    width: "100%",
    preserveAspectRatio: "xMidYMid meet",
    "font-family": ss.font,
    "font-weight": ss.fontWeight,
  }) as SVGSVGElement;
  svg.style.minWidth = `${Math.min(W, 540)}px`;
  svg.style.maxWidth = "100%";
  svg.style.display = "block";
  svg.style.background = ss.bg;
  svg.appendChild(defs(ss));

  // title
  svg.appendChild(
    n(
      "text",
      {
        x: padX,
        y: padY + titleH * 0.7,
        "font-size": 16,
        fill: ss.named,
      },
      `${spec.name}  ·  ${spec.total} bits`,
    ),
  );

  const rowsTop = padY + titleH;
  const indexY = rowsTop;
  const valueY = rowsTop + indexH;

  const baseG = n("g");
  if (ss.filter) baseG.setAttribute("filter", `url(#${ss.filter})`);
  svg.appendChild(baseG);

  // outer panel rect (wobbled)
  baseG.appendChild(
    n("path", {
      d: rectPath(padX, rowsTop, innerW, indexH + valueH, rng, ss.wobbleAmp),
      fill: ss.panel,
      stroke: ss.stroke,
      "stroke-width": ss.strokeWidth,
      "stroke-linejoin": "round",
      "stroke-linecap": "round",
    }),
  );

  // horizontal divider between index and value rows
  baseG.appendChild(
    n("path", {
      d: wobblePath(
        [
          [padX, valueY],
          [padX + innerW, valueY],
        ],
        rng,
        ss.wobbleAmp,
      ),
      fill: "none",
      stroke: ss.stroke,
      "stroke-width": ss.strokeWidth,
      "stroke-linecap": "round",
    }),
  );

  // members
  let bitOffset = 0;
  spec.members.forEach((m, i) => {
    const x = padX + bitOffset * bw;
    const w = m.size * bw;

    // cell background fill (value row only)
    const fillColor = ss.cellFill(m, i);
    if (fillColor !== "none" && fillColor !== "transparent") {
      baseG.appendChild(
        n("rect", {
          x: x + 1,
          y: valueY + 1,
          width: w - 2,
          height: valueH - 2,
          rx: ss.cornerRadius,
          ry: ss.cornerRadius,
          fill: fillColor,
          opacity: style === "crayon" ? 0.85 : 0.9,
        }),
      );
    }

    // crayon hatch / extras inside cell
    ss.cellExtras(baseG, m, x, valueY, w, valueH, rng);

    // vertical separator on the right side (skip last to avoid double-line on outer rect)
    if (i < spec.members.length - 1) {
      baseG.appendChild(
        n("path", {
          d: wobblePath(
            [
              [x + w, indexY],
              [x + w, valueY + valueH],
            ],
            rng,
            ss.wobbleAmp,
          ),
          fill: "none",
          stroke: ss.stroke,
          "stroke-width": ss.strokeWidth,
          "stroke-linecap": "round",
        }),
      );
    }

    // index row text — msb on left, lsb on right (or single)
    const idxFontSize = 11;
    const idxY = indexY + indexH * 0.65;
    if (m.size === 1) {
      svg.appendChild(
        n(
          "text",
          {
            x: x + w / 2,
            y: idxY,
            "font-size": idxFontSize,
            fill: ss.index,
            "text-anchor": "middle",
          },
          String(m.msb),
        ),
      );
    } else {
      svg.appendChild(
        n(
          "text",
          {
            x: x + 4,
            y: idxY,
            "font-size": idxFontSize,
            fill: ss.index,
            "text-anchor": "start",
          },
          String(m.msb),
        ),
      );
      svg.appendChild(
        n(
          "text",
          {
            x: x + w - 4,
            y: idxY,
            "font-size": idxFontSize,
            fill: ss.index,
            "text-anchor": "end",
          },
          String(m.lsb),
        ),
      );
    }

    // value row content
    const valYBase = valueY + valueH * 0.62;
    if (m.kind === "named") {
      svg.appendChild(
        n(
          "text",
          {
            x: x + w / 2,
            y: valYBase,
            "font-size": 15,
            "font-weight": ss.fontWeight,
            fill: ss.named,
            "text-anchor": "middle",
          },
          m.name ?? "",
        ),
      );
    } else {
      const bits = (m.value ?? "").split("");
      bits.forEach((ch, bi) => {
        const cx = x + ((bi + 0.5) / bits.length) * w;
        svg.appendChild(
          n(
            "text",
            {
              x: cx,
              y: valYBase,
              "font-size": 15,
              "font-weight": ss.fontWeight,
              fill: ss.immediate,
              "text-anchor": "middle",
            },
            ch,
          ),
        );
      });
    }

    bitOffset += m.size;
  });

  container.appendChild(svg);
}
