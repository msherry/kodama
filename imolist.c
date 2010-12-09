#include "imolist.h"

/* does an efficient append to SLL, using a tail pointer */
void
slist_append(GSList **head, GSList **tail, gpointer data)
{
  *tail = g_slist_append(*tail, data);
  if(*head == NULL)
  {
    *head = *tail;
  }
  else
  {
    *tail = g_slist_nth(*tail, 1);
  }
}

/* deletes the first node in a SLL */
void
slist_delete_first(GSList **head, GSList **tail)
{
  GSList *node = g_slist_nth(*head, 0);
  *head = g_slist_delete_link(*head, node);

  if(*head == NULL)
    *tail = NULL;
}

/* deletes the first node in a SLL */
void
slist_delete_nth(GSList **head, GSList **tail, int which)
{
  if(which < 0)
    return;

  if(which == 0)
    slist_delete_first(head, tail);
  else
  {
    GSList *node = g_slist_nth(*head, which);
    if(!node)
      return;

    *head = g_slist_delete_link(*head, node);
    *tail = g_slist_last(*head);
  }
}
