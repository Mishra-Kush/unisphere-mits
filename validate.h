#ifndef VALIDATE_H
#define VALIDATE_H
#define COLLEGE_DOMAIN "mitsgwl.ac.in"
int valid_email_format(char *email);          /* lowercases in place; 1 = well-formed */
int is_college_email(const char *email);      /* exactly @mitsgwl.ac.in (email already lowercased) */
int valid_password(const char *pw, const char **why);
int valid_full_name(const char *name);
int valid_enrollment(char *enrollment);       /* uppercases in place */
#endif
