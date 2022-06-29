# Terragraph Documentation
This directory contains all Terragraph manuals and documentation that can be
used as a reference for developers and operators.

## Table of Contents
1. [Developer Manual](developer/README.md)
2. [Runbook](runbook/README.md)
3. [Whitepapers](whitepapers/README.md)

## Building
> **These build scripts are deprecated. Please refer to the `docusaurus` directory.**

This documentation can be compiled into a single HTML file per directory using
[pandoc](https://pandoc.org/). Run the included script `build/build_docs.sh` to
generate all of the documents.

```bash
$ ./build/build_docs.sh <output_dir>
```

This will create `*.html` file(s) and make a copy of the `media/` directory in
the output location.

## Printing
The HTML files can be printed to PDFs using headless Chrome and
[puppeteer](https://github.com/GoogleChrome/puppeteer). Run the included script
`build/print_docs.sh` to generate a PDF from an input HTML file.

```bash
$ ./build/print_docs.sh <input_html> <output_pdf>
```
