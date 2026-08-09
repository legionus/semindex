# Indexing pipeline

This library orchestrates parsing, fingerprinting, provenance collection,
symbol storage, and compiler command storage for one indexing request.

Keep cross-library sequencing and result ownership here. Detailed parsing,
database implementation, and command-line option handling belong to the
libraries that provide those capabilities.
