#ifndef _IMOLIST_H_
#define _IMOLIST_H_

#include <glib.h>

void slist_append(GSList **head, GSList **tail, gpointer data);
void slist_delete_first(GSList **head, GSList **tail);
void slist_delete_nth(GSList **head, GSList **tail, int which);

#endif

