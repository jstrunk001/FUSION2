// List of figures and list of tables for the PDF manual, placed right
// after the table of contents. Quarto gives its figures and tables the
// custom Typst kinds "quarto-float-fig" and "quarto-float-tbl", so the
// outlines target those kinds rather than Typst's plain image/table ones.
#heading(level: 1, numbering: none)[List of Figures]
#outline(title: none, target: figure.where(kind: "quarto-float-fig"))

#heading(level: 1, numbering: none)[List of Tables]
#outline(title: none, target: figure.where(kind: "quarto-float-tbl"))

// Let long tables continue onto the next page. Quarto places each
// captioned table inside a figure, and Typst keeps figures on one page by
// default, so a table longer than a page would otherwise run off the
// bottom. This rule applies to every table after this point in the book.
#show figure.where(kind: "quarto-float-tbl"): set block(breakable: true)

// Table cells are narrow, so justified text in them spreads words apart;
// set table text ragged-right instead.
#show table: set par(justify: false)
