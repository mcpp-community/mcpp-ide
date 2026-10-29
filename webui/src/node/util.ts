/** `node:util` for the page code: promisify, which the process module applies at load. */
export function promisify<T extends (...args: any[]) => void>(fn: T): (...args: any[]) => Promise<any> {
  return (...args: any[]) => new Promise((resolve, reject) => fn(...args, (error: unknown, ...values: unknown[]) => {
    if (error) reject(error); else resolve(values.length > 1 ? values : values[0]);
  }));
}
export default { promisify };
