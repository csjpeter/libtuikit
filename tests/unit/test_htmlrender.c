/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* Unit tests of tui_html_render (TUI-124): every tag, entities, colours, links, tables, broken markup. */

/*
 * The text is compared byte for byte; the SGR form is also fed to
 * libptytest's emulator and the cells (text, colour, attributes) are
 * compared there.
 */

#include "test_helpers.h"
#include "tuikit/tuikit.h"

#include <errno.h>
#include <string.h>

int g_tests_run = 0;
int g_tests_failed = 0;

static const char *render(const char *html, int width, int ansi) {
    static tui_buf b;
    tui_buf_free(&b);
    tui_buf_init(&b);
    if (tui_html_render(html, width, ansi, &b) != 0) return "(error)";
    return b.data ? b.data : "";
}

static const char *plain(const char *html) { return render(html, 40, 0); }
static const char *sgr(const char *html) { return render(html, 40, 1); }

/* The SGR rendering on an emulated screen of `rows` x `cols`. */
static PtySession *screen_of(const char *html, int rows, int cols) {
    PtySession *s = pty_open(cols, rows);
    const char *t = render(html, cols, 1);
    for (const char *p = t; *p; p++) {
        if (*p == '\n') pty_feed(s, "\r\n", 2);
        else pty_feed(s, p, 1);
    }
    return s;
}

static void test_args(void) {
    ASSERT_EQ_STR(plain(NULL), "", "NULL is the empty text");
    ASSERT_EQ_STR(plain(""), "", "an empty text");
    ASSERT_EQ_STR(plain("  \n "), "", "only white space");
    ASSERT_EQ_STR(plain("<p></p><div> </div>"), "", "only empty blocks");
    tui_buf b;
    tui_buf_init(&b);
    errno = 0;
    ASSERT_EQ_INT(tui_html_render("x", 40, 0, NULL), -1, "no output buffer");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_html_render(NULL, 40, 0, &b), 0, "NULL html is fine");
    tui_buf_puts(&b, "keep:");
    ASSERT_EQ_INT(tui_html_render("x", 40, 0, &b), 0, "appends");
    ASSERT_EQ_STR(b.data, "keep:x", "to the end of the buffer");
    tui_buf_free(&b);
}

static void test_text_and_entities(void) {
    ASSERT_EQ_STR(plain("hello   world\n\t again"), "hello world again", "white space collapses");
    ASSERT_EQ_STR(plain("&amp; &lt; &gt; &quot; &apos;"), "& < > \" '", "the basic entities");
    ASSERT_EQ_STR(plain("&#65;&#x42;&#X43;"), "ABC", "decimal and hexadecimal");
    ASSERT_EQ_STR(plain("&eacute;&odblac;&udblac;&Aacute;"), "\xC3\xA9\xC5\x91\xC5\xB1\xC3\x81",
                  "Latin-1 and Hungarian letters");
    ASSERT_EQ_STR(plain("a&nbsp;b"), "a\xC2\xA0" "b", "a non-breaking space stays one");
    ASSERT_EQ_STR(plain("&hellip;&mdash;&euro;&copy;"), "\xE2\x80\xA6\xE2\x80\x94\xE2\x82\xAC\xC2\xA9", "typography");
    ASSERT_EQ_STR(plain("&bogus; &amp &#xZZ; &#; &#65"), "&bogus; &amp &#xZZ; &#; &#65", "unknown or unfinished stay as written");
    ASSERT_EQ_STR(plain("&#0; &#xD800; &#x110000;"), "\xEF\xBF\xBD \xEF\xBF\xBD \xEF\xBF\xBD", "invalid code points are U+FFFD");
    ASSERT_EQ_STR(plain("a < b and 1<2 </ x"), "a < b and 1<2 </ x", "a stray < is text");
    ASSERT_EQ_STR(plain("a\x01" "b\x7f" "c\xC2\x85" "d"), "a b c d", "controls are spaces");
    ASSERT_EQ_STR(plain("a\xE2\x80\xAE" "b\xEF\xBB\xBF" "c\xE2\x80\x8F" "d"), "abcd", "bidi controls and BOM are dropped");
    ASSERT_EQ_STR(plain("\xFF" "x"), "\xEF\xBF\xBD" "x", "malformed UTF-8 is U+FFFD");
}

static void test_inline_styles(void) {
    ASSERT_EQ_STR(plain("<b>bold</b> <i>it</i> <u>ul</u> <s>st</s> <code>c</code>"), "bold it ul st c",
                  "plain text drops the styles");
    ASSERT_EQ_STR(sgr("<b>bold</b>"), "\x1b[1mbold\x1b[0m", "b is SGR 1");
    ASSERT_EQ_STR(sgr("<strong>x</strong>"), "\x1b[1mx\x1b[0m", "strong is bold");
    ASSERT_EQ_STR(sgr("<i>x</i>"), "\x1b[3mx\x1b[0m", "i is SGR 3");
    ASSERT_EQ_STR(sgr("<em>x</em>"), "\x1b[3mx\x1b[0m", "em is italic");
    ASSERT_EQ_STR(sgr("<u>x</u>"), "\x1b[4mx\x1b[0m", "u is SGR 4");
    ASSERT_EQ_STR(sgr("<s>x</s><strike>y</strike><del>z</del>"),
                  "\x1b[9mxyz\x1b[0m", "s, strike and del are SGR 9, equal neighbours merge");
    ASSERT_EQ_STR(sgr("<code>x</code>"), "\x1b[36mx\x1b[0m", "code is cyan");
    ASSERT_EQ_STR(sgr("<tt>x</tt><kbd>y</kbd>"), "\x1b[36mxy\x1b[0m", "tt and kbd too");
    ASSERT_EQ_STR(sgr("<b><i>x</i></b>"), "\x1b[1;3mx\x1b[0m", "nested styles add up");
    ASSERT_EQ_STR(sgr("<b>a <i>b</i> c</b>"), "\x1b[1ma \x1b[0m\x1b[1;3mb\x1b[0m\x1b[1m c\x1b[0m",
                  "a span ends and the outer one goes on");
    ASSERT_EQ_STR(sgr("a<b>b</b>c"), "a\x1b[1mb\x1b[0mc", "inside a word");
    ASSERT_EQ_STR(plain("x<br>y<br/>z<BR />w"), "x\ny\nz\nw", "br in every spelling");
    ASSERT_EQ_STR(plain("a<br><br>b"), "a\n\nb", "two br make a blank line");
    ASSERT_EQ_STR(plain("a<br>"), "a", "a trailing br adds nothing");
    ASSERT_EQ_STR(plain("<span>x</span><small>y</small><sup>z</sup>"), "xyz", "unknown inline tags keep their text");
    ASSERT_EQ_STR(plain("<IMG SRC=x ALT=\"a &amp; b\">"), "[a & b]", "an image is its alt text");
    ASSERT_EQ_STR(plain("<img src=http://x.org/p.png>"), "", "no alt, nothing; no load");
    ASSERT_EQ_STR(plain("a <!-- hidden --> b"), "a b", "comments are dropped");
}

static void test_colours(void) {
    ASSERT_EQ_STR(sgr("<font color=\"#ff0000\">x</font>"), "\x1b[38;2;255;0;0mx\x1b[0m", "font color #rrggbb");
    ASSERT_EQ_STR(sgr("<span style=\"color:#0f8\">x</span>"), "\x1b[38;2;0;255;136mx\x1b[0m", "style color #rgb");
    ASSERT_EQ_STR(sgr("<span style=\"color: rgb(1, 2, 3)\">x</span>"), "\x1b[38;2;1;2;3mx\x1b[0m", "rgb()");
    ASSERT_EQ_STR(sgr("<span style=\"COLOR:Red;\">x</span>"), "\x1b[38;2;255;0;0mx\x1b[0m", "a name, any case");
    ASSERT_EQ_STR(sgr("<p style='color:blue'>x</p>"), "\x1b[38;2;0;0;255mx\x1b[0m", "on a block");
    ASSERT_EQ_STR(sgr("<font color=nonsense>x</font>"), "x", "an unknown colour is ignored");
    ASSERT_EQ_STR(sgr("<span style=\"color:#12\">x</span><span style=\"color:#12345g\">y</span>"), "xy", "bad hex");
    ASSERT_EQ_STR(sgr("<span style=\"color:rgb(1,2)\">a</span><span style=\"color:rgb(1,2,3\">b</span>"
                      "<span style=\"color:rgb(,2,3)\">c</span><span style=\"color:rgb(1,2,3,4)\">d</span>"), "abcd", "bad rgb()");
    ASSERT_EQ_STR(sgr("<span style=\"color:rgb(999,0,0)\">x</span>"), "\x1b[38;2;255;0;0mx\x1b[0m", "rgb() clamps");
    ASSERT_EQ_STR(plain("<font color=red>x</font>"), "x", "plain text has no colour");
    ASSERT_EQ_STR(sgr("<span style=\"font-weight:bold\">x</span>"), "\x1b[1mx\x1b[0m", "font-weight bold");
    ASSERT_EQ_STR(sgr("<span style=\"font-weight:700\">x</span>"), "\x1b[1mx\x1b[0m", "font-weight 700");
    ASSERT_EQ_STR(sgr("<b><span style=\"font-weight:normal\">x</span></b>"), "x", "normal clears bold");
    ASSERT_EQ_STR(sgr("<span style=\"font-style:italic\">x</span>"), "\x1b[3mx\x1b[0m", "font-style italic");
    ASSERT_EQ_STR(sgr("<i><span style=\"font-style:normal\">x</span></i>"), "x", "normal clears italic");
    ASSERT_EQ_STR(sgr("<span style=\"text-decoration:underline\">x</span>"), "\x1b[4mx\x1b[0m", "underline");
    ASSERT_EQ_STR(sgr("<span style=\"text-decoration: line-through\">x</span>"), "\x1b[9mx\x1b[0m", "line-through");
    ASSERT_EQ_STR(sgr("<a href=\"#\"><span style=\"text-decoration:none\">x</span></a>"), "x"
                  , "none clears the link underline");
    ASSERT_EQ_STR(sgr("<code style=\"color:#010203\">x</code>"), "\x1b[38;2;1;2;3mx\x1b[0m", "a colour beats the code cyan");
    ASSERT_EQ_STR(sgr("<span style=\"background:red;;novalue;:x\">x</span>"), "x", "other css and junk are ignored");
}

static void test_emulated_cells(void) {
    PtySession *s = screen_of("plain <b>bold</b> <i>it</i> <u>ul</u> <s>st</s> <font color=\"#336699\">col</font>"
                              "<a href=\"https://x.org/\">go</a>", 3, 60);
    ASSERT_ROW_CONTAINS(s, 0, "plain bold it ul st col");
    ASSERT_EQ_INT(pty_cell_attr(s, 0, 0), 0, "plain has no attribute");
    ASSERT_EQ_INT(pty_cell_attr(s, 0, 6), PTY_ATTR_BOLD, "bold");
    ASSERT_EQ_INT(pty_cell_attr(s, 0, 11), PTY_ATTR_ITALIC, "italic");
    ASSERT_EQ_INT(pty_cell_attr(s, 0, 14), PTY_ATTR_UNDERLINE, "underline");
    ASSERT_EQ_INT(pty_cell_attr(s, 0, 17), PTY_ATTR_STRIKE, "strike");
    ASSERT_EQ_INT(pty_cell_fg(s, 0, 20), PTY_COLOR_RGB(0x33, 0x66, 0x99), "the colour");
    ASSERT_EQ_INT(pty_cell_fg(s, 0, 22), PTY_COLOR_RGB(0x33, 0x66, 0x99), "to its last letter");
    ASSERT_EQ_INT(pty_cell_fg(s, 0, 23), 0, "and no further");
    ASSERT_EQ_INT(pty_cell_attr(s, 0, 23), PTY_ATTR_UNDERLINE, "the link is underlined");
    ASSERT_EQ_INT(pty_cell_attr(s, 0, 26), 0, "its URL is not");
    pty_close(s);

    s = screen_of("<code>x</code>", 2, 20);
    ASSERT_EQ_INT(pty_cell_fg(s, 0, 0), PTY_FG_CYAN, "code is cyan");
    pty_close(s);
}

static void test_style_across_lines(void) {
    ASSERT_EQ_STR(render("<b>aaa bbb ccc ddd eee fff ggg hhh iii jjj</b>", 20, 1),
                  "\x1b[1maaa bbb ccc ddd eee\x1b[0m\n\x1b[1mfff ggg hhh iii jjj\x1b[0m",
                  "a style open at a break is closed and opened again");
    PtySession *s = screen_of("<b>aaa bbb ccc ddd eee fff ggg hhh iii jjj</b> tail", 4, 20);
    ASSERT_EQ_INT(pty_cell_attr(s, 1, 0), PTY_ATTR_BOLD, "bold on the second line");
    ASSERT_EQ_INT(pty_cell_attr(s, 1, 16), PTY_ATTR_BOLD, "to its end");
    ASSERT_ROW_CONTAINS(s, 1, "fff ggg hhh iii jjj");
    ASSERT_ROW_CONTAINS(s, 2, "tail");
    ASSERT_EQ_INT(pty_cell_attr(s, 2, 0), 0, "the text after the span is plain");
    pty_close(s);
    ASSERT_EQ_STR(render("<b>x</b> <b>y</b>", 40, 1), "\x1b[1mx\x1b[0m \x1b[1my\x1b[0m", "the blank between is not bold");
    ASSERT_EQ_STR(render("<blockquote><b>aaaa bbbb cccc dddd eeee ffff</b></blockquote>", 20, 1),
                  "> \x1b[1maaaa bbbb cccc\x1b[0m\n> \x1b[1mdddd eeee ffff\x1b[0m",
                  "the prefix is not styled, the style is reopened after it");
}

static void test_blocks(void) {
    ASSERT_EQ_STR(plain("<p>one</p><p>two</p>"), "one\n\ntwo", "paragraphs are set apart");
    ASSERT_EQ_STR(plain("<div>one</div><div>two</div>"), "one\ntwo", "divs are lines");
    ASSERT_EQ_STR(plain("text<div>block</div>more"), "text\nblock\nmore", "a block breaks the line");
    ASSERT_EQ_STR(plain("<h1>Head</h1>text"), "Head\n\ntext", "a heading is set apart");
    ASSERT_EQ_STR(sgr("<h2>Head</h2>"), "\x1b[1mHead\x1b[0m", "and bold");
    ASSERT_EQ_STR(plain("<h3>a</h3><h4>b</h4><h5>c</h5><h6>d</h6>"), "a\n\nb\n\nc\n\nd", "h3 to h6");
    ASSERT_EQ_STR(plain("a<hr>b"), "a\n\n\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\n\nb",
                  "hr is a rule");
    ASSERT_EQ_STR(render("<hr>", 20, 0), "\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80",
                  "as wide as the text");
    ASSERT_EQ_STR(plain("<center>c</center><section>s</section><address>a</address><dl><dt>t</dt><dd>d</dd></dl>"),
                  "c\ns\na\nt\nd", "the other block elements");
}

static void test_lists(void) {
    ASSERT_EQ_STR(plain("<ul><li>one</li><li>two</li></ul>"), " \xE2\x80\xA2 one\n \xE2\x80\xA2 two", "bullets");
    ASSERT_EQ_STR(plain("<ol><li>a</li><li>b</li></ol>"), " 1. a\n 2. b", "numbers");
    ASSERT_EQ_STR(plain("<ol start=5><li>a<li>b</ol>"), " 5. a\n 6. b", "start");
    ASSERT_EQ_STR(plain("<ol start=-3><li>a</ol><ol start=999999999999><li>b</ol><ol start=x><li>c</ol>"),
                  " 1. a\n\n 99999. b\n\n 1. c", "start is clamped");
    ASSERT_EQ_STR(plain("<ul><li>a<ul><li>b<ul><li>c</ul></ul></ul>"),
                  " \xE2\x80\xA2 a\n    \xE2\x97\xA6 b\n       \xE2\x80\xA2 c", "nested lists, bullets alternate");
    ASSERT_EQ_STR(plain("<ul><li>x</ul>after"), " \xE2\x80\xA2 x\n\nafter", "a list is set apart");
    ASSERT_EQ_STR(plain("before<ul><li>x</ul>"), "before\n\n \xE2\x80\xA2 x", "from the text before");
    ASSERT_EQ_STR(plain("<ol><li>a<ul><li>b</ul><li>c</ol>"), " 1. a\n     \xE2\x97\xA6 b\n 2. c", "a list in an item, the counter goes on");
    ASSERT_EQ_STR(render("<ul><li>aaaa bbbb cccc dddd eeee ffff gggg</ul>", 20, 0),
                  " \xE2\x80\xA2 aaaa bbbb cccc\n   dddd eeee ffff\n   gggg", "an item wraps under its text");
    ASSERT_EQ_STR(plain("<li>loose</li><li>items</li>"), " \xE2\x80\xA2 loose\n \xE2\x80\xA2 items", "li without a list");
    ASSERT_EQ_STR(plain("<ul><li>a<li>b</ul>"), " \xE2\x80\xA2 a\n \xE2\x80\xA2 b", "li end tags are optional");
    ASSERT_EQ_STR(plain("<ul><li><p>p1</p><p>p2</p></li></ul>"), " \xE2\x80\xA2 p1\n\n   p2", "paragraphs in an item");
    ASSERT_EQ_STR(plain("<ul></ul><ol></ol>"), "", "empty lists");
    ASSERT_EQ_STR(plain("<ul><ul><ul><ul><ul><ul><ul><ul><ul><li>deep</ul></ul></ul></ul></ul></ul></ul></ul></ul>"),
                  " \xE2\x97\xA6 deep", "nesting is bounded at eight levels");
}

static void test_quotes_and_pre(void) {
    ASSERT_EQ_STR(plain("<blockquote>quoted</blockquote>after"), "> quoted\n\nafter", "a quote");
    ASSERT_EQ_STR(plain("<blockquote>a<br>b</blockquote>"), "> a\n> b", "every line has the prefix");
    ASSERT_EQ_STR(plain("<blockquote>a<blockquote>b</blockquote></blockquote>"), "> a\n\n> > b", "nested quotes");
    ASSERT_EQ_STR(plain("<blockquote><ul><li>x</ul></blockquote>"), ">  \xE2\x80\xA2 x", "a list in a quote");
    ASSERT_EQ_STR(render("<blockquote>aaaa bbbb cccc dddd eeee</blockquote>", 20, 0), "> aaaa bbbb cccc\n> dddd eeee",
                  "a quote wraps with its prefix");
    ASSERT_EQ_STR(plain("<pre>\n  a   b\n\n c</pre>"), "    a   b\n\n   c", "pre keeps spaces and blank lines, first newline dropped");
    ASSERT_EQ_STR(plain("<pre>a\tb</pre>"), "  a b", "a tab is a blank");
    ASSERT_EQ_STR(plain("<pre>  \n\n</pre>"), "", "an empty pre");
    ASSERT_EQ_STR(plain("x<pre>y</pre>z"), "x\n\n  y\n\nz", "pre is set apart");
    ASSERT_EQ_STR(render("<pre>0123456789012345678901234</pre>", 20, 0), "  012345678901234567\n  8901234",
                  "a long line is split at the column");
    ASSERT_EQ_STR(sgr("<pre>a<b>b</b>c</pre>"), "  a\x1b[1mb\x1b[0mc", "styles in pre");
    ASSERT_EQ_STR(plain("<pre>a<br>b</pre>"), "  a\n  b", "br in pre");
    ASSERT_EQ_STR(plain("<pre>a\r\nb</pre>"), "  a\n  b", "CR is dropped");
    ASSERT_EQ_STR(plain("<pre>a</pre>"), "  a", "unstyled pre");
    ASSERT_EQ_STR(plain("<pre>a<div>b</div></pre>"), "  a\n  b", "a block inside pre");
}

static void test_links(void) {
    ASSERT_EQ_STR(plain("<a href=\"https://example.org/x\">site</a>"), "site (https://example.org/x)", "https");
    ASSERT_EQ_STR(plain("<a href='http://example.org'>site</a>"), "site (http://example.org)", "http, single quotes");
    ASSERT_EQ_STR(plain("<a href=mailto:a@example.org>write</a>"), "write (mailto:a@example.org)", "mailto, unquoted");
    ASSERT_EQ_STR(plain("<a href=\"https://example.org\">https://example.org</a>"), "https://example.org", "the text is the URL");
    ASSERT_EQ_STR(plain("<a href=\"https://example.org\"></a>"), "https://example.org", "an empty text shows the URL");
    ASSERT_EQ_STR(plain("<a href=\"https://example.org\"><img src=x></a>"), "https://example.org", "an image link shows the URL");
    ASSERT_EQ_STR(plain("<a href=\"javascript:alert(1)\">js</a>"), "js", "javascript: is not shown");
    ASSERT_EQ_STR(plain("<a href=\"JaVaScRiPt:alert(1)\">js</a><a href=\"data:text/html,x\">d</a><a href=\"cid:x\">c</a>"
                        "<a href=\"/rel\">r</a><a href=\"ftp://x\">f</a><a href=\"file:///etc/passwd\">p</a>"), "jsdcrfp", "no other target");
    ASSERT_EQ_STR(plain("<a href=\"https://x.org/a b\">sp</a><a href=\"https://x.org/\xC3\xA9\">u</a>"), "spu", "no blank or non-ASCII in a shown URL");
    ASSERT_EQ_STR(plain("<a href=\"https://\">e</a><a href=\"http://\">e</a><a href=\"\">e</a><a>e</a>"), "eeee", "no host, no URL, no href");
    ASSERT_EQ_STR(plain("<a href=\"https://x.org/?a=1&amp;b=2\">q</a>"), "q (https://x.org/?a=1&b=2)", "entities in the URL");
    ASSERT_EQ_STR(plain("<a href=\"https://x.org/&#x6a;s\">q</a>"), "q (https://x.org/js)", "numeric entities in the URL");
    ASSERT_EQ_STR(plain("<a href=\"https://x.org/&bogus;\">q</a>"), "q (https://x.org/&bogus;)", "an unknown entity in an attribute stays");
    char big[1200];
    strcpy(big, "<a href=\"https://x.org/");
    memset(big + strlen(big), 'a', 700);
    strcpy(big + strlen(big), "\">long</a>");
    ASSERT_EQ_STR(plain(big), "long", "a URL over 500 bytes is not shown");
    ASSERT_EQ_STR(plain("<a href=\"https://x.org\">a <b>b</b></a>"), "a b (https://x.org)", "styled link text");
    ASSERT_EQ_STR(sgr("<a href=\"https://x.org\">go</a>"), "\x1b[4mgo\x1b[0m (https://x.org)", "underlined, the URL not");
    ASSERT_EQ_STR(plain("<a href=\"https://x.org\"><div>x</div></a>"), "x\n(https://x.org)", "a block in a link");
    ASSERT_EQ_STR(plain("<A HREF=\"https://x.org\" TITLE=t>x</A>"), "x (https://x.org)", "upper case and other attributes");
}

static void test_tables(void) {
    ASSERT_EQ_STR(plain("<table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>two</td></tr></table>"),
                  "  A \xE2\x94\x82 B\n  \xE2\x94\x80\xE2\x94\x80\xE2\x94\xBC\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\n  1 \xE2\x94\x82 two",
                  "a header row is ruled");
    ASSERT_EQ_STR(plain("<table><tr><td>a<td>b<tr><td>c<td>d</table>"), "  a \xE2\x94\x82 b\n  c \xE2\x94\x82 d", "no header, optional end tags");
    ASSERT_EQ_STR(plain("<table><tbody><tr><td>a</td></tr></tbody></table>after"), "  a\n\nafter", "tbody and the text after");
    ASSERT_EQ_STR(plain("x<table><tr><td>a</td></tr></table>"), "x\n\n  a", "set apart from the text before");
    ASSERT_EQ_STR(sgr("<table><tr><th>H</th></tr><tr><td><b>b</b></td></tr></table>"),
                  "  \x1b[1mH\x1b[0m\n  \xE2\x94\x80\n  \x1b[1mb\x1b[0m", "th is bold, cells carry their styles");
    ASSERT_EQ_STR(plain("<table><tr><td>a</td><td></td><td>c</td></tr><tr><td>1</td></tr></table>"),
                  "  a \xE2\x94\x82  \xE2\x94\x82 c\n  1", "short and empty cells");
    ASSERT_EQ_STR(plain("<table><tr><td>a<br>b<p>c</p><ul><li>d</ul></td></tr></table>"), "  a b c \xE2\x80\xA2 d", "blocks in a cell are blanks");
    ASSERT_EQ_STR(plain("<table><tr><td>a<hr>b</td></tr></table>"), "  a b", "hr in a cell");
    ASSERT_EQ_STR(plain("<table><tr><td>a<table><tr><td>n1<td>n2</table>z</td></tr></table>"), "  a n1 n2 z", "a nested table is flattened");
    ASSERT_EQ_STR(plain("<table>stray<tr>stray<td>a</td>stray</tr></table>"), "  a", "text outside the cells is dropped");
    ASSERT_EQ_STR(plain("<table><td>a</td><td>b</td></table>"), "  a \xE2\x94\x82 b", "cells without a row");
    ASSERT_EQ_STR(plain("<table></table><table><tr></tr></table>x"), "x", "empty tables");
    ASSERT_EQ_STR(plain("<tr><td>a</td></tr>x"), "ax", "a row outside a table is text");
    ASSERT_EQ_STR(plain("<table><tr><th>a</th></tr><tr></tr><tr><td>b</td></tr></table>"), "  a\n  \xE2\x94\x80\n  b", "an empty row");
    ASSERT_EQ_STR(render("<table><tr><td>aaaaaaaaaaaaaaaaaaaaaaaa</td><td>bbbb</td></tr></table>", 20, 0),
                  "  aaaaaaa\xE2\x80\xA6 \xE2\x94\x82 bbbb",
                  "the widest column is shortened with an ellipsis");
    ASSERT_EQ_STR(render("<table><tr><td><b>aaaaaaaaaaaaaaaaaaaaaaaa</b></td><td>bbbb</td></tr></table>", 20, 1),
                  "  \x1b[1maaaaaaa\x1b[0m\xE2\x80\xA6 \xE2\x94\x82 bbbb",
                  "a style cut inside is closed");
    ASSERT_EQ_STR(render("<table><tr><td>\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2</td><td>b</td></tr></table>", 20, 0),
                  "  \xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE2\x80\xA6 \xE2\x94\x82 b", "wide characters count two");
    ASSERT_EQ_STR(render("<ul><li><table><tr><td>a</td></tr></table></ul>", 40, 0), " \xE2\x80\xA2   a", "a table in a list item");
    ASSERT_EQ_STR(render("<blockquote><table><tr><td>a<td>b</table></blockquote>", 40, 0), ">   a \xE2\x94\x82 b", "a table in a quote");

    /* beyond the bounds: rows and columns are cut, not overrun */
    tui_buf big;
    tui_buf_init(&big);
    tui_buf_puts(&big, "<table>");
    for (int r = 0; r < 140; r++) {
        tui_buf_puts(&big, "<tr>");
        for (int c = 0; c < 20; c++)
            tui_buf_puts(&big, "<td>x</td>");
        tui_buf_puts(&big, "</tr>");
    }
    tui_buf_puts(&big, "</table>");
    tui_buf out;
    tui_buf_init(&out);
    ASSERT_EQ_INT(tui_html_render(big.data, 200, 0, &out), 0, "a big table renders");
    int lines = 1;
    for (size_t i = 0; i < out.len; i++) lines += out.data[i] == '\n';
    ASSERT_EQ_INT(lines, 128, "128 rows");
    tui_buf_free(&big);
    tui_buf_free(&out);
}

static void test_dropped_content(void) {
    ASSERT_EQ_STR(plain("a<script>alert('<b>x</b>')</script>b"), "ab", "script and its content");
    ASSERT_EQ_STR(plain("a<SCRIPT type=x>if (a < b) {}</Script >b"), "ab", "in any case");
    ASSERT_EQ_STR(plain("a<style>p { color: red }</style>b"), "ab", "style");
    ASSERT_EQ_STR(plain("a<script src=\"http://evil.example/x.js\"></script>b"), "ab", "no remote script");
    ASSERT_EQ_STR(plain("a<script>never closed <b>x"), "a", "an unclosed script runs to the end");
    ASSERT_EQ_STR(plain("a<script>x</scr"), "a", "an unfinished end tag");
    ASSERT_EQ_STR(plain("<html><head><title>T</title><meta charset=utf-8><link rel=stylesheet href=http://x.org/s.css></head><body>text</body></html>"),
                  "text", "head, title, meta, link");
    ASSERT_EQ_STR(plain("<head><title>T</title>text after an unclosed head<body>b"), "b", "an unclosed head ends at body");
    ASSERT_EQ_STR(plain("<head><title>T</title></head>x<head>y</head>z"), "xz", "head again");
    ASSERT_EQ_STR(plain("a<iframe src=http://x.org><b>x</b></iframe>b<object data=x>o</object>c<embed src=x>d"), "abcd", "iframe, object, embed");
    ASSERT_EQ_STR(plain("a<svg><g><text>x</text></g></svg>b<math>m</math>c<template><b>t</b></template>d"), "abcd", "svg, math, template");
    ASSERT_EQ_STR(plain("a<select><option>o</option></select>b<textarea>t</textarea>c<applet>x</applet>d"), "abcd", "select, textarea, applet");
    ASSERT_EQ_STR(plain("a<object>x<object>y</object>z</object>b"), "ab", "skipping nests");
    ASSERT_EQ_STR(plain("a<svg/>b<iframe/>c"), "abc", "a self-closed skipped tag skips nothing");
    ASSERT_EQ_STR(plain("a<object>x</b>y<p>z</object>b"), "ab", "stray tags in a skipped part");
    ASSERT_EQ_STR(plain("<!DOCTYPE html><?xml version=\"1.0\"?>x"), "x", "declarations");
    ASSERT_EQ_STR(plain("x<!-- never closed"), "x", "an unclosed comment");
    ASSERT_EQ_STR(plain("x<!doctype"), "x", "an unfinished declaration");
    ASSERT_EQ_STR(plain("<noscript>kept</noscript>"), "kept", "noscript text is shown");
    ASSERT_EQ_STR(plain("<body onload=\"evil()\" style=\"color:red\">x</body>"), "x", "event handlers are never read");
    ASSERT_EQ_STR(plain("<img src=\"http://track.example/p.gif\" onerror=\"x()\">x"), "x", "no load");
}

static void test_broken_markup(void) {
    ASSERT_EQ_STR(plain("<b>unclosed <i>text"), "unclosed text", "unclosed tags");
    ASSERT_EQ_STR(sgr("<b>x"), "\x1b[1mx\x1b[0m", "unclosed styles are closed at the end");
    ASSERT_EQ_STR(plain("</b></p></div></table>text</li>"), "text", "stray end tags");
    ASSERT_EQ_STR(sgr("<b>a<i>b</b>c</i>d"), "\x1b[1ma\x1b[0m\x1b[1;3mb\x1b[0mcd", "mis-nested: the end tag closes down to its element");
    ASSERT_EQ_STR(plain("a <b"), "a", "an unfinished tag at the end");
    ASSERT_EQ_STR(plain("a <b class=\"x"), "a", "an unterminated attribute value");
    ASSERT_EQ_STR(plain("a <b class='x>y</b>"), "a", "an unterminated quote swallows the rest of the tag");
    ASSERT_EQ_STR(plain("a<>b<  >c</>d"), "a<>b< >c</>d", "no tag name, text");
    ASSERT_EQ_STR(plain("<1>x</2>"), "<1>x</2>", "a digit starts no name");
    ASSERT_EQ_STR(plain("<p>a<p>b<p>c"), "a\n\nb\n\nc", "p end tags are optional");
    ASSERT_EQ_STR(plain("<b >x</b >"), "x", "blank before >");
    ASSERT_EQ_STR(plain("<a  href = \"https://x.org\"  >x</a>"), "x (https://x.org)", "blanks around =");
    ASSERT_EQ_STR(plain("<p class=a/>x"), "x", "self-closed non-void");
    ASSERT_EQ_STR(plain("<div/>a<div/>b"), "a\nb", "self-closed block");
    ASSERT_EQ_STR(plain("<verylongunknowntagname>x</verylongunknowntagname>"), "x", "a long tag name");
    ASSERT_EQ_STR(plain("<a verylongattributenamethatgoesonandon=\"1\" =x href=\"https://x.org\">x</a>"), "x (https://x.org)", "odd attributes");
    ASSERT_EQ_STR(plain("<ul><li>a</p></ul>b"), " \xE2\x80\xA2 a\n\nb", "an end tag that does not match is ignored");
    ASSERT_EQ_STR(plain("<td>a<th>b<tr>c"), "abc", "cells and rows outside a table");

    /* deep nesting is bounded, not overrun */
    tui_buf big, out;
    tui_buf_init(&big);
    tui_buf_init(&out);
    for (int i = 0; i < 300; i++) tui_buf_puts(&big, "<div><b>");
    tui_buf_puts(&big, "deep");
    for (int i = 0; i < 300; i++) tui_buf_puts(&big, "</b></div>");
    ASSERT_EQ_INT(tui_html_render(big.data, 40, 1, &out), 0, "deep nesting renders");
    ASSERT(strstr(out.data, "deep") != NULL, "and keeps the text");
    tui_buf_reset(&out);
    tui_buf_reset(&big);
    for (int i = 0; i < 700; i++) tui_buf_puts(&big, "<font color=\"#00");
    tui_buf_reset(&big);
    for (int i = 0; i < 700; i++) {
        char t[64];
        snprintf(t, sizeof t, "<font color=\"#%06x\">c</font>", i);
        tui_buf_puts(&big, t);
    }
    ASSERT_EQ_INT(tui_html_render(big.data, 40, 1, &out), 0, "more distinct styles than the table holds");
    ASSERT(strstr(out.data, "\x1b[38;2;0;0;1m") != NULL, "the first ones are coloured");
    tui_buf_free(&big);
    tui_buf_free(&out);
}

static void test_width(void) {
    ASSERT_EQ_STR(render("aaaa bbbb cccc dddd eeee ffff", 20, 0), "aaaa bbbb cccc dddd\neeee ffff", "wraps at the width");
    ASSERT_EQ_STR(render("aaaa bbbb cccc dddd eeee ffff", 5, 0), "aaaa bbbb cccc dddd\neeee ffff", "the least width is 20");
    ASSERT_EQ_STR(render("0123456789012345678901234", 20, 0), "01234567890123456789\n01234", "a long word is split");
    ASSERT_EQ_STR(render("ab 0123456789012345678901234", 20, 0), "ab\n01234567890123456789\n01234", "after a short word");
    ASSERT_EQ_STR(render("\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2", 20, 0),
                  "\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\n\xE6\xBC\xA2",
                  "wide characters are two columns");
    ASSERT_EQ_STR(render("a\xCC\x81" "b", 20, 0), "a\xCC\x81" "b", "a combining mark has no width");
    ASSERT_EQ_STR(render("\xCC\x81 x", 20, 0), "\xCC\x81 x", "a lone combining mark");
    char deep[400];
    strcpy(deep, "");
    for (int i = 0; i < 8; i++) strcat(deep, "<blockquote>");
    strcat(deep, "aaaa bbbb");
    ASSERT_EQ_STR(render(deep, 20, 0), "> > > > > > > > aaaa\n> > > > > > > > bbbb", "a prefix wider than half the line");
    for (int i = 0; i < 8; i++) strcat(deep, "</blockquote>");
    strcpy(deep, "");
    for (int i = 0; i < 12; i++) strcat(deep, "<ol><li>");
    strcat(deep, "abc");
    ASSERT(strstr(render(deep, 20, 0), "abc") != NULL, "a prefix as wide as the line still shows the text");
    ASSERT_EQ_STR(render("<blockquote><blockquote><blockquote><blockquote><blockquote><blockquote><blockquote><blockquote>x</blockquote></blockquote></blockquote></blockquote></blockquote></blockquote></blockquote></blockquote>"
                         "<ul><ul><ul><ul><ul><ul><ul><ul><li>0123456789</ul></ul></ul></ul></ul></ul></ul></ul>", 20, 0),
                  "> > > > > > > > x\n\n \xE2\x97\xA6 0123456789"
                  , "no more than eight levels");
}

static void test_realistic_mail(void) {
    const char *mail =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>.a{color:red}</style></head>"
        "<body style=\"font-family:sans\"><div>Hi <b>Anna</b>,</div><br>"
        "<p>The report is <a href=\"https://example.org/r\">here</a>; <font color=\"#c00\">urgent</font>.</p>"
        "<ul><li>first</li><li>second</li></ul>"
        "<blockquote>On Monday you wrote:<br>thanks</blockquote>"
        "<table><tr><th>Item</th><th>Qty</th></tr><tr><td>nuts</td><td>3</td></tr></table>"
        "<script>track()</script><img src=\"http://t.example/p.gif\"></body></html>";
    ASSERT_EQ_STR(render(mail, 40, 0),
                  "Hi Anna,\n\nThe report is here\n(https://example.org/r); urgent.\n\n"
                  " \xE2\x80\xA2 first\n \xE2\x80\xA2 second\n\n> On Monday you wrote:\n> thanks\n\n"
                  "  Item \xE2\x94\x82 Qty\n  \xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xBC\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\n  nuts \xE2\x94\x82 3",
                  "a mail body");
}

static const TestCase tests[] = {
    TEST_CASE(test_args),
    TEST_CASE(test_text_and_entities),
    TEST_CASE(test_inline_styles),
    TEST_CASE(test_colours),
    TEST_CASE(test_emulated_cells),
    TEST_CASE(test_style_across_lines),
    TEST_CASE(test_blocks),
    TEST_CASE(test_lists),
    TEST_CASE(test_quotes_and_pre),
    TEST_CASE(test_links),
    TEST_CASE(test_tables),
    TEST_CASE(test_dropped_content),
    TEST_CASE(test_broken_markup),
    TEST_CASE(test_width),
    TEST_CASE(test_realistic_mail),
};

int main(int argc, char **argv) {
    return RUN_SUITE("htmlrender", tests, 200, 60);
}
