/*
 * Copyright 2026 bong-water-water-bong
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// The onebit backend card's logo (TOS3-C26 item 2): the 1bit engine mark is ours, inlined, and
// rendered on the card for the `onebit` recipe. Rendered here rather than pattern-matched, so a
// component that stops drawing the mark fails instead of still looking wired up.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const ts = require('typescript');
const React = require('react');
const { renderToStaticMarkup } = require('react-dom/server');

const componentPath = path.resolve(__dirname, '../src/components/OnebitEngineLogo.tsx');
const managerPath = path.resolve(__dirname, '../src/components/BackendManager.tsx');
const componentSource = fs.readFileSync(componentPath, 'utf8');
const managerSource = fs.readFileSync(managerPath, 'utf8');

// Our own file, so it carries the notice.
assert.match(componentSource, /Copyright 2026 bong-water-water-bong/);
assert.match(componentSource, /SPDX-License-Identifier: Apache-2\.0/);

// The appliance has no network: the mark may not be fetched, and it may not need a font either.
// (the only URL allowed is the license header's)
assert.doesNotMatch(componentSource, /https?:\/\/(?!www\.apache\.org)/);
assert.doesNotMatch(componentSource, /fetch\(|<img|url\(|\.png|\.woff|XMLHttpRequest/);

const compiled = ts.transpileModule(componentSource, {
  compilerOptions: {
    target: ts.ScriptTarget.ES2022,
    module: ts.ModuleKind.CommonJS,
    jsx: ts.JsxEmit.React,
    esModuleInterop: true,
  },
  fileName: componentPath,
  reportDiagnostics: true,
});
const errors = (compiled.diagnostics || []).filter(d => d.category === ts.DiagnosticCategory.Error);
assert.equal(errors.length, 0, errors.map(d => ts.flattenDiagnosticMessageText(d.messageText, '\n')).join('\n'));

const module_ = { exports: {} };
new Function('require', 'module', 'exports', compiled.outputText)(require, module_, module_.exports);
const markup = renderToStaticMarkup(React.createElement(module_.exports.OnebitEngineLogo));

// 2x2 relay cubes = four rects, one lit at a time, and the reduced-motion branch that keeps a
// still frame meaningful.
assert.equal((markup.match(/<rect/g) || []).length, 4, markup);
assert.match(markup, /aria-label="1bit engine"/);
assert.match(markup, /viewBox="0 0 24 24"/);
assert.match(markup, /@keyframes onebit-relay/);
assert.match(markup, /prefers-reduced-motion/);
// The relay animation names a different delay per cube, or all four light together.
const delays = new Set(markup.match(/animation-delay: \d+s/g) || []);
assert.equal(delays.size, 4, [...delays].join(','));
assert.match(markup, /data-backend-logo-mark/);

// And it reaches the card: the `onebit` recipe has the label the logo's wordmark needs, and the
// card's logo branch renders our mark instead of going to the remote engines directory.
assert.match(managerSource, /import \{ OnebitEngineLogo \} from '\.\/OnebitEngineLogo';/);
assert.match(managerSource, /onebit:\s+'1bit engine',/);
assert.match(managerSource, /recipe === 'onebit' \? \(\s*(?:\/\/[^\n]*\n\s*)*<OnebitEngineLogo \/>/);
assert.doesNotMatch(managerSource, /ENGINE_LOGOS[\s\S]{0,400}onebit:/, 'onebit must not be a remote logo');

console.log('onebit logo: PASS');
