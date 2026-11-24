// node tools/test_extension_lib.mjs -- the tile's pure helpers.
// The SAME cases lookit_selftest runs against core/TileText, so the shell's
// cleaning and the app's can't drift apart unnoticed.
import {cleaned, sendRequest, MAX_CHARS} from '../extension/lookit-tile@scott8420.github.io/lib.js';

let pass = 0, fail = 0;
function check(name, ok, detail = '') {
    if (ok) { pass++; console.log(`  PASS  ${name}`); }
    else { fail++; console.log(`  FAIL  ${name}  ${detail}`); }
}

check('tile: CRLF -> LF, trailing blanks off, ends trimmed',
      cleaned('  One.  \r\nTwo. \t\r\n') === 'One.\nTwo.', JSON.stringify(cleaned('  One.  \r\nTwo. \t\r\n')));
check('tile: blank runs squeezed to one blank line', cleaned('A.\n\n\n\nB.') === 'A.\n\nB.');
check('tile: blanks inside a line are kept', cleaned('a  b\tc') === 'a  b\tc');
check('tile: lone CR is a line break', cleaned('a\rb') === 'a\nb');
check('tile: not a string -> empty', cleaned(null) === '' && cleaned(undefined) === '');
check('tile: empty line -> nothing, no error', JSON.stringify(sendRequest('   ', 'line')) === '{}');
check('tile: empty clipboard / selection -> its own error',
      sendRequest('', 'clipboard').error === 'The clipboard has no text.' &&
      sendRequest('\n', 'selection').error === 'Nothing is selected.');
check('tile: text -> cleaned text', sendRequest(' The ball was thrown by Sam. ').text === 'The ball was thrown by Sam.');
check('tile: too long -> error, nothing sent',
      !!sendRequest('x'.repeat(MAX_CHARS + 1)).error && !sendRequest('x'.repeat(MAX_CHARS + 1)).text);
check('tile: exactly the cap is fine', sendRequest('x'.repeat(MAX_CHARS)).text?.length === MAX_CHARS);
check('tile: the cap counts characters, not bytes', [...'été'].length === 3);

console.log('-----------------------------------------------');
console.log(`${pass} pass / ${fail} fail`);
process.exit(fail ? 1 : 0);
