/* FunPay Vertex HTML utilities. */

#ifndef FPV_HTML_H
#define FPV_HTML_H

#include <stdbool.h>
#include <stddef.h>

#include <libxml/HTMLparser.h>
#include <libxml/tree.h>

typedef struct fpv_html_doc {
  htmlDocPtr doc;
} fpv_html_doc_t;

typedef struct fpv_html_node_list {
  xmlNode** nodes;
  size_t count;
} fpv_html_node_list_t;

fpv_html_doc_t* fpv_html_parse(const char* html, size_t length);
void fpv_html_destroy(fpv_html_doc_t* doc);

bool fpv_html_node_has_class(const xmlNode* node, const char* class_name);
xmlNode* fpv_html_find_first_by_class(
    xmlNode* node,
    const char* tag,
    const char* class_name);
fpv_html_node_list_t fpv_html_find_all_by_class(
    xmlNode* node,
    const char* tag,
    const char* class_name);
void fpv_html_node_list_destroy(fpv_html_node_list_t* list);

char* fpv_html_node_text(const xmlNode* node);
char* fpv_html_node_attr(const xmlNode* node, const char* name);

#endif
