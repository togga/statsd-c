/*
 * STATSD-C
 * C port of Etsy's node.js-based statsd server
 *
 * originally based on http://github.com/jbuchbinder/statsd-c
 *
 */

#ifndef STRINGS_H_INCLUDED
#define STRINGS_H_INCLUDED

void sanitize_key(char *k);
void sanitize_value(char *k);
void appendstring(char *orig, char *addition);
char *ltoa(long l);
char *ldtoa(long double ld);

#endif
