# orange-book template (FUSION2 copy)

A copy of the `orange-book` 0.7.1 Typst book template (MIT No Attribution,
see `LICENSE`) that Quarto uses for PDF books. `../../typst-show.typ` imports
this copy instead of the packaged one. Three changes from the original, both in
`lib.typ`:

- Parts and chapters start on the next page rather than the next odd page,
  so the PDF has no blank pages before them.
- Section headings (levels 2-4) are "sticky": Typst moves a heading to the
  next page with its text rather than leaving it alone at a page bottom.
- Chapter and appendix numbers carry no trailing period, so a reference
  reads "Chapter 12 shows" rather than "Chapter 12. shows".
