# Dat Archive
A library and file format for combining multiple files into a single archive

## Repo Structure
### Specification
The actual format of the archive file can be found in [Archive File Spec.md](./Archive File Spec.md).

This file defines the structure of the format, explains what each part is for, and defines some rules.

### Library
The library for handling this format can be found in the [source](./source/) and [include](./include/) directories,
which contain the cpp implementation and header files respectively.

The library can be easily added to a cmake project by using `add_subdiretory` with the root directory of this repo and
linking with: `target_link_libraries(your-project dat-archive)`.

### Example
An example (poorly) demonstrating the use of the library can be found in the [examples](./examples/) directory. The 
files used in the directory are from my personal desktop so probably won't be found on your system.

## Dependencies
This project depends on [ZLib](https://www.zlib.net/).