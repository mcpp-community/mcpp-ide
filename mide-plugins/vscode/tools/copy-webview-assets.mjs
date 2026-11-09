import fs from 'node:fs';
import { build } from 'esbuild';
fs.mkdirSync('dist/vendor',{recursive:true});
fs.copyFileSync('node_modules/@mermaid-js/tiny/dist/mermaid.tiny.js','dist/vendor/mermaid.js');
fs.copyFileSync('node_modules/@mermaid-js/tiny/LICENSE','dist/vendor/MERMAID-LICENSE');
// The tests page (../../webui/src/tests.ts), the one mcpp-clion draws in JCEF, for the Test Insights webview.
await build({ entryPoints: ['../../webui/src/tests.ts'], bundle: true, format: 'iife', platform: 'browser', target: 'es2022',
              outfile: 'dist/webui/tests.js', logLevel: 'warning', legalComments: 'eof' });
