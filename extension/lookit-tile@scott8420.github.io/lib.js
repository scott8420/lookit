// Lookit Tile -- the tile's pure helpers (no gi:// imports), so they are tested
// with node outside the shell: node tools/test_extension_lib.mjs
//
// The JS twin of core/TileText (C++). The app cleans and caps again on arrival,
// because the same action is also reached from `lookit --send`. Change one,
// change the other; both tests run the same cases.

export const MAX_CHARS = 20000;   // a capture is a few lines, not a chapter

// Line endings made plain, trailing space off each line, blank runs squeezed,
// ends trimmed. Lines and paragraphs keep their shape.
export function cleaned(text) {
    if (typeof text !== 'string')
        return '';
    return text
        .replace(/\r\n?/g, '\n')
        .replace(/[ \t]+\n/g, '\n')
        .replace(/\n{3,}/g, '\n\n')
        .trim();
}

// {text} to send, or {error} to show, or {} for nothing at all.
// where: 'line' | 'clipboard' | 'selection'
export function sendRequest(raw, where = 'line') {
    const text = cleaned(raw);
    if (!text)
        return where === 'line' ? {}
            : {error: where === 'clipboard' ? 'The clipboard has no text.' : 'Nothing is selected.'};
    const n = [...text].length;   // characters (code points), as the app counts
    if (n > MAX_CHARS)
        return {error: `That's ${n} characters -- the tile takes up to ${MAX_CHARS} at a time.`};
    return {text};
}
