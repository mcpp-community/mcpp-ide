import Mocha from "mocha";
import path from "node:path";

export function run(): Promise<void> {
  const mocha = new Mocha({ ui: "tdd", color: true, timeout: 30_000 });
  // MCPP_E2E_NO_BACKEND: the declared degraded mode, without the backend; else the whole suite.
  mocha.addFile(path.resolve(__dirname, process.env.MCPP_E2E_NO_BACKEND === "1" ? "nobackend.e2e.js" : "extension.e2e.js"));
  return new Promise((resolvePromise, reject) => {
    mocha.run((failures) => {
      if (failures === 0) {
        resolvePromise();
      } else {
        reject(new Error(`${failures} Extension Host smoke test(s) failed.`));
      }
    });
  });
}
