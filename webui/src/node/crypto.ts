/** `node:crypto` for the page code: SHA-256 digests and random bytes. */
import { sha256 } from "@noble/hashes/sha256";
import { Buffer } from "buffer";

export function randomBytes(size: number): Buffer {
  const bytes = new Uint8Array(size);
  globalThis.crypto.getRandomValues(bytes);
  return Buffer.from(bytes);
}

class Hash {
  private readonly chunks: Uint8Array[] = [];
  constructor(algorithm: string) {
    if (algorithm !== "sha256") throw new Error(`the workbench has no ${algorithm} digest`);
  }
  update(data: string | Uint8Array): this {
    this.chunks.push(typeof data === "string" ? new TextEncoder().encode(data) : data);
    return this;
  }
  digest(encoding?: "hex" | "base64"): string | Buffer {
    const all = Buffer.concat(this.chunks);
    const out = Buffer.from(sha256(all));
    return encoding ? out.toString(encoding) : out;
  }
}

export function createHash(algorithm: string): Hash {
  return new Hash(algorithm);
}

export default { randomBytes, createHash };
