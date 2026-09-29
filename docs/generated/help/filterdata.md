```text
Usage: filterdata <input.las/laz or directory> [other /options]
Point Cloud Filtering Tool (Elevation, Return Type, Classification, Scan Angle)

Options:
  /return:<value>	Return numbers to keep: /return:1 or /return:1,2 for an explicit list (ranges like /return:1-2 also work), or the mnemonics /return:first, /return:last, /return:only, /return:intermediate. Omit to keep every return. (default: )
  /class:<value>	Point classifications to keep. Omit for the default (excludes ASPRS noise classes 7 and 18); /class:all or /class:* keeps every class; /class:2,3,4,5 or /class:1-5 whitelists the listed classes; /class:~7,9,18 blacklists them (keeps every other class). (default: )
  /maxz:<value>	Maximum Z elevation threshold (default: )
  /minz:<value>	Minimum Z elevation threshold (default: )
  /output:<value>	Output filtered LAS/LAZ file path (default: filtered_output.laz)
```
