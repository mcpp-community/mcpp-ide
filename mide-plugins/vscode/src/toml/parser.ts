// mcpp.toml read tolerantly, for the views that list what a manifest declares (its dependencies,
// its targets): the structure an edit in progress already has, with exact 0-based ranges, never an
// exception for a half-typed `[`, string, array or inline table. The rules layer -- completion,
// hover, the jumps, the diagnostics -- is the mcpp-ide backend's (design 2026-10-30 part 2 §8), whose
// scanner answers the same structure (its tests/rules goldens were written by this one).
//
// The subset read: table headers (`[[...]]` arrays of tables, quoted segments), bare, quoted and
// dotted keys, strings (`"""` and `'''` multi-line ones too), integers, booleans, arrays and inline
// tables (nested, across lines), and comments. A CRLF line end is dropped before the scan; columns
// count the text without it.

/** 0 基行列范围，end 为开区间（与 VS Code Range 同构）。 */
export interface TomlRange {
  startLine: number;
  startCharacter: number;
  endLine: number;
  endCharacter: number;
}

/** 键/段路径中的一段：已去引号的名字 + 原始 token 的范围（含引号）。 */
export interface TomlKeySegment {
  name: string;
  quoted: boolean;
  range: TomlRange;
}

export type TomlValueKind = "string" | "integer" | "boolean" | "array" | "inlineTable" | "unknown";

/** 值节点。open = true 表示结构未闭合（输入到一半），range 延伸到文档末尾。 */
export interface TomlValueNode {
  kind: TomlValueKind;
  range: TomlRange;
  /** 未闭合（字符串缺右引号、数组缺 ]、内联表缺 }）。 */
  open: boolean;
  /** 标量原文：字符串为去引号内容（未做转义还原），整数/布尔为原文 token。 */
  text?: string;
  /** 字符串节点：引号字符。 */
  quote?: '"' | "'";
  /** 字符串节点：是否三引号多行串。 */
  multiline?: boolean;
  /** 字符串节点：内容范围（不含引号；未闭合时延伸到扫描终点）。 */
  contentRange?: TomlRange;
  /** 数组节点：已解析出的元素。 */
  elements?: TomlValueNode[];
  /** 内联表节点：已解析出的键值条目。 */
  entries?: TomlKeyValueNode[];
}

/** 键值对节点：keyPath 为点分键拆出的段数组（如 capi.lua → [capi, lua]）。 */
export interface TomlKeyValueNode {
  type: "keyValue";
  keyPath: TomlKeySegment[];
  range: TomlRange;
  /** 缺省表示 `=` 之后没有可用值（含尚未输入的情况）。 */
  value?: TomlValueNode;
}

/** 段头节点。open = true 表示 `]` 尚未输入。 */
export interface TomlSectionNode {
  type: "section";
  /** 段路径段（已去引号，如 [target.'cfg(windows)'.build] → target / cfg(windows) / build）。 */
  segments: TomlKeySegment[];
  /** 是否为 [[...]] 数组表段头。 */
  isArray: boolean;
  open: boolean;
  range: TomlRange;
  /** 段头所在行（0 基）。 */
  line: number;
}

export type TomlNode = TomlSectionNode | TomlKeyValueNode;

export interface TomlDocument {
  nodes: TomlNode[];
}

interface Pos {
  line: number;
  col: number;
}

function isBareKeyChar(ch: string): boolean {
  return (
    (ch >= "a" && ch <= "z") ||
    (ch >= "A" && ch <= "Z") ||
    (ch >= "0" && ch <= "9") ||
    ch === "_" ||
    ch === "-"
  );
}

/** 逐行容错扫描器。行列均为 0 基，列按剥除 \r 后的文本计算。 */
class Scanner {
  private readonly lines: string[];
  private line = 0;
  private col = 0;

  constructor(lines: readonly string[]) {
    this.lines = lines.map((text) => (text.endsWith("\r") ? text.slice(0, -1) : text));
    if (this.lines.length === 0) {
      this.lines.push("");
    }
  }

  private pos(): Pos {
    return { line: this.line, col: this.col };
  }

  private eof(): boolean {
    return this.line >= this.lines.length;
  }

  private atEol(): boolean {
    return this.eof() || this.col >= this.lines[this.line].length;
  }

  private peek(): string {
    return this.atEol() ? "" : this.lines[this.line][this.col];
  }

  private peekAt(offset: number): string {
    if (this.eof()) {
      return "";
    }
    const text = this.lines[this.line];
    return this.col + offset < text.length ? text[this.col + offset] : "";
  }

  private advance(): void {
    if (this.eof()) {
      return;
    }
    this.col += 1;
    if (this.col > this.lines[this.line].length) {
      this.line += 1;
      this.col = 0;
    }
  }

  private samePos(a: Pos, b: Pos): boolean {
    return a.line === b.line && a.col === b.col;
  }

  private skipInlineWs(): void {
    while (this.peek() === " " || this.peek() === "\t") {
      this.col += 1;
    }
  }

  /** 跳过空白、换行与行注释（# 到行尾；字符串内的 # 不会走到这里）。 */
  private skipTrivia(): void {
    while (!this.eof()) {
      const ch = this.peek();
      if (ch === " " || ch === "\t") {
        this.col += 1;
        continue;
      }
      if (this.atEol()) {
        this.line += 1;
        this.col = 0;
        continue;
      }
      if (ch === "#") {
        while (!this.atEol()) {
          this.col += 1;
        }
        continue;
      }
      break;
    }
  }

  private rangeFrom(start: Pos): TomlRange {
    return { startLine: start.line, startCharacter: start.col, endLine: this.line, endCharacter: this.col };
  }

  private sliceText(a: Pos, b: Pos): string {
    // A scan that ran to the end of the document (an unclosed multi-line string) ends one line past
    // the last; its text ends where the last line does.
    if (b.line >= this.lines.length) {
      const last = this.lines.length - 1;
      b = { line: last, col: this.lines[last].length };
      if (a.line > last) {
        return "";
      }
    }
    if (a.line === b.line) {
      return this.lines[a.line].slice(a.col, b.col);
    }
    const parts = [this.lines[a.line].slice(a.col)];
    for (let l = a.line + 1; l < b.line; l += 1) {
      parts.push(this.lines[l]);
    }
    parts.push(this.lines[b.line].slice(0, b.col));
    return parts.join("\n");
  }

  scan(): TomlNode[] {
    const nodes: TomlNode[] = [];
    while (!this.eof()) {
      this.skipTrivia();
      if (this.eof()) {
        break;
      }
      const start = this.pos();
      if (this.peek() === "[") {
        nodes.push(this.scanHeader(start));
      } else {
        const keyValue = this.scanKeyValue();
        if (keyValue !== undefined) {
          nodes.push(keyValue);
        }
      }
      // 进度保护：任何无法识别的垃圾字符都必须被消费，保证容错扫描必然终止。
      if (this.samePos(this.pos(), start)) {
        this.advance();
      }
    }
    return nodes;
  }

  /** 段头：[a.b] / [[a.b]] / 引号段；未闭合（缺 ]）时 open = true 并跳过该行剩余内容。 */
  private scanHeader(start: Pos): TomlSectionNode {
    this.advance(); // [
    let isArray = false;
    if (this.peek() === "[") {
      isArray = true;
      this.advance();
    }
    const segments: TomlKeySegment[] = [];
    let open = true;
    for (;;) {
      this.skipInlineWs();
      const segment = this.scanKeySegment();
      if (segment === undefined) {
        break;
      }
      segments.push(segment);
      this.skipInlineWs();
      if (this.peek() === ".") {
        this.advance();
        continue;
      }
      break;
    }
    this.skipInlineWs();
    if (this.peek() === "]") {
      this.advance();
      if (isArray && this.peek() === "]") {
        this.advance();
      }
      // 容错：[[x] 只写了一个 ] 也视为已闭合。
      open = false;
    }
    if (open) {
      // 未闭合段头：跳过该行剩余内容，避免残余字符被当作下一条语句。
      while (!this.atEol()) {
        this.col += 1;
      }
    }
    return { type: "section", segments, isArray, open, range: this.rangeFrom(start), line: start.line };
  }

  /** 单个键/段路径段：裸键或单/双引号键（引号键跨行即按未闭合截断）。 */
  private scanKeySegment(): TomlKeySegment | undefined {
    const start = this.pos();
    const quote = this.peek();
    if (quote === '"' || quote === "'") {
      this.advance();
      const contentStart = this.pos();
      while (!this.atEol() && this.peek() !== quote) {
        // 双引号键内的转义（如 \"）不结束键。
        if (quote === '"' && this.peek() === "\\" && this.col + 1 < this.lines[this.line].length) {
          this.col += 2;
          continue;
        }
        this.col += 1;
      }
      const contentEnd = this.pos();
      if (this.peek() === quote) {
        this.advance();
      }
      return { name: this.sliceText(contentStart, contentEnd), quoted: true, range: this.rangeFrom(start) };
    }
    if (!isBareKeyChar(quote)) {
      return undefined;
    }
    while (isBareKeyChar(this.peek())) {
      this.col += 1;
    }
    return { name: this.sliceText(start, this.pos()), quoted: false, range: this.rangeFrom(start) };
  }

  /** 键值对：点分键 [= 值]。无法识别出键也没有值时返回 undefined。 */
  private scanKeyValue(): TomlKeyValueNode | undefined {
    const start = this.pos();
    const segments: TomlKeySegment[] = [];
    for (;;) {
      this.skipInlineWs();
      const segment = this.scanKeySegment();
      if (segment === undefined) {
        break;
      }
      segments.push(segment);
      this.skipInlineWs();
      if (this.peek() === ".") {
        this.advance();
        continue;
      }
      break;
    }
    this.skipInlineWs();
    let value: TomlValueNode | undefined;
    if (this.peek() === "=") {
      this.advance();
      this.skipInlineWs();
      // 值必须在同行开始（TOML 本就如此）；行尾没有值则按缺失处理。
      if (!this.atEol() && this.peek() !== "#") {
        value = this.scanValue();
      }
    }
    if (segments.length === 0 && value === undefined) {
      return undefined;
    }
    return { type: "keyValue", keyPath: segments, range: this.rangeFrom(start), value };
  }

  private scanValue(): TomlValueNode {
    const ch = this.peek();
    if (ch === '"' || ch === "'") {
      return this.scanStringValue();
    }
    if (ch === "[") {
      return this.scanArrayValue();
    }
    if (ch === "{") {
      return this.scanInlineTableValue();
    }
    return this.scanBareValue();
  }

  /** 字符串：单/双引号单行串与三引号多行串；未闭合时 open = true。 */
  private scanStringValue(): TomlValueNode {
    const start = this.pos();
    const quote = this.peek() as '"' | "'";
    this.advance();
    let multiline = false;
    let open = true;
    let contentStart = this.pos();
    let contentEnd = this.pos();
    if (this.peek() === quote) {
      this.advance();
      if (this.peek() === quote) {
        multiline = true;
        this.advance();
        contentStart = this.pos();
      } else {
        open = false;
      }
    }
    if (open) {
      while (!this.eof()) {
        if (!multiline && this.atEol()) {
          break;
        }
        const ch = this.peek();
        if (ch === "\\" && quote === '"') {
          this.advance();
          if (!this.eof() && (multiline || !this.atEol())) {
            this.advance();
          }
          continue;
        }
        if (ch === quote) {
          if (multiline) {
            if (this.peekAt(1) === quote && this.peekAt(2) === quote) {
              contentEnd = this.pos();
              this.advance();
              this.advance();
              this.advance();
              open = false;
              break;
            }
            this.advance();
            continue;
          }
          contentEnd = this.pos();
          this.advance();
          open = false;
          break;
        }
        this.advance();
      }
      if (open) {
        contentEnd = this.pos();
      }
    }
    return {
      kind: "string",
      range: this.rangeFrom(start),
      open,
      text: this.sliceText(contentStart, contentEnd),
      quote,
      multiline,
      contentRange: {
        startLine: contentStart.line,
        startCharacter: contentStart.col,
        endLine: contentEnd.line,
        endCharacter: contentEnd.col,
      },
    };
  }

  /** 数组：可跨行，元素递归解析；缺 ] 时 open = true。 */
  private scanArrayValue(): TomlValueNode {
    const start = this.pos();
    this.advance(); // [
    const elements: TomlValueNode[] = [];
    let open = true;
    for (;;) {
      this.skipTrivia();
      if (this.eof()) {
        break;
      }
      const ch = this.peek();
      if (ch === "]") {
        this.advance();
        open = false;
        break;
      }
      if (ch === ",") {
        this.advance();
        continue;
      }
      const elementStart = this.pos();
      elements.push(this.scanValue());
      if (this.samePos(this.pos(), elementStart)) {
        this.advance();
      }
    }
    return { kind: "array", range: this.rangeFrom(start), open, elements };
  }

  /** 内联表：可嵌套、可跨行，条目是完整键值对；缺 } 时 open = true。 */
  private scanInlineTableValue(): TomlValueNode {
    const start = this.pos();
    this.advance(); // {
    const entries: TomlKeyValueNode[] = [];
    let open = true;
    for (;;) {
      this.skipTrivia();
      if (this.eof()) {
        break;
      }
      const ch = this.peek();
      if (ch === "}") {
        this.advance();
        open = false;
        break;
      }
      if (ch === ",") {
        this.advance();
        continue;
      }
      const entryStart = this.pos();
      const entry = this.scanKeyValue();
      if (entry !== undefined) {
        entries.push(entry);
      }
      if (this.samePos(this.pos(), entryStart)) {
        this.advance();
      }
    }
    return { kind: "inlineTable", range: this.rangeFrom(start), open, entries };
  }

  /** 裸值 token：布尔、整数，其余归为 unknown（容错，不校验合法性）。 */
  private scanBareValue(): TomlValueNode {
    const start = this.pos();
    let end = this.pos();
    while (!this.atEol()) {
      const ch = this.peek();
      if (ch === "," || ch === "]" || ch === "}" || ch === "[" || ch === "{" || ch === "#") {
        break;
      }
      this.col += 1;
      if (ch !== " " && ch !== "\t") {
        end = this.pos();
      }
    }
    const text = this.sliceText(start, end);
    let kind: TomlValueKind = "unknown";
    if (text === "true" || text === "false") {
      kind = "boolean";
    } else if (/^[+-]?\d[\d_]*$/.test(text)) {
      kind = "integer";
    }
    const range: TomlRange = { startLine: start.line, startCharacter: start.col, endLine: end.line, endCharacter: end.col };
    return { kind, range, open: false, text };
  }
}

/** 容错解析整个文档，产出带 0 基行列范围的节点树。永不抛异常。 */
export function parseMcppToml(lines: readonly string[]): TomlDocument {
  return { nodes: new Scanner(lines).scan() };
}
