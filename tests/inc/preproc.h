/* shared header for the preprocessor test: pulled in with a
 * quote-form #include resolved against the including file's
 * directory */

#ifndef PREPROC_H
#define PREPROC_H

#define INC_DIR_VALUE 42
#define INC_ADD(a, b) ((a) + (b))

#endif