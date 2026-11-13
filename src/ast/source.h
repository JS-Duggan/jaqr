#pragma once

// Zero-based source coordinates, with an exclusive end position. Keeping the original
// span on AST nodes lets later passes report errors against JAQR rather than generated C++.
struct source_span {
    int start_line = 0;
    int start_col = 0;
    int end_line = 0;
    int end_col = 0;
};
