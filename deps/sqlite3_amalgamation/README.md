# Vendored SQLite amalgamation

`sqlite3.c` / `sqlite3.h` -- SQLite 3.45.1 amalgamation, downloaded from
`https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip` (public domain).
Used by `fusion::table::TableWriter`'s SQLite output path
(`src/libfusion_core/TableWriter.cpp`) -- a single-file, single-writer table
sink, independent of the vendored static GDAL build (see
`build_gdal_minimal.ps1`, which does not compile in any OGR vector driver).

To update: download a newer amalgamation zip from https://www.sqlite.org/download.html,
replace these two files, and rebuild.
