/* FunPay Vertex HTML utilities. */

#include "fpv_html.h"

#include <stdlib.h>
#include <string.h>

#include "fpv_string.h"

fpv_html_doc_t* fpv_html_parse(const char* html, size_t length) {
  if (!html) {
    return NULL;
  }

  htmlDocPtr doc = htmlReadMemory(
      html,
      (int)length,
      NULL,
      NULL,
      HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET);
  if (!doc) {
    return NULL;
  }

  fpv_html_doc_t* wrapper = (fpv_html_doc_t*)calloc(1, sizeof(*wrapper));
  if (!wrapper) {
    xmlFreeDoc(doc);
    return NULL;
  }
  wrapper->doc = doc;
  return wrapper;
}

void fpv_html_destroy(fpv_html_doc_t* doc) {
  if (!doc) {
    return;
  }
  if (doc->doc) {
    xmlFreeDoc(doc->doc);
  }
  free(doc);
}

bool fpv_html_node_has_class(const xmlNode* node, const char* class_name) {
  if (!node || !class_name || !class_name[0]) {
    return false;
  }

  xmlChar* value = xmlGetProp((xmlNode*)node, (const xmlChar*)"class");
  if (!value) {
    return false;
  }

  const char* start = (const char*)value;
  size_t target_len = strlen(class_name);
  bool match = false;

  while (*start != '\0') {
    while (*start == ' ') {
      start++;
    }
    if (*start == '\0') {
      break;
    }
    const char* end = start;
    while (*end != '\0' && *end != ' ') {
      end++;
    }
    size_t len = (size_t)(end - start);
    if (len == target_len && strncmp(start, class_name, len) == 0) {
      match = true;
      break;
    }
    start = end;
  }

  xmlFree(value);
  return match;
}

xmlNode* fpv_html_find_first_by_class(
    xmlNode* node,
    const char* tag,
    const char* class_name) {
  for (xmlNode* cur = node; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (!tag || (cur->name && strcmp((const char*)cur->name, tag) == 0)) {
        if (!class_name || fpv_html_node_has_class(cur, class_name)) {
          return cur;
        }
      }
      if (cur->children) {
        xmlNode* found =
            fpv_html_find_first_by_class(cur->children, tag, class_name);
        if (found) {
          return found;
        }
      }
    }
  }
  return NULL;
}

static void fpv_html_collect_by_class(
    xmlNode* node,
    const char* tag,
    const char* class_name,
    fpv_html_node_list_t* list) {
  for (xmlNode* cur = node; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (!tag || (cur->name && strcmp((const char*)cur->name, tag) == 0)) {
        if (!class_name || fpv_html_node_has_class(cur, class_name)) {
          xmlNode** grown = (xmlNode**)realloc(
              list->nodes,
              (list->count + 1) * sizeof(*grown));
          if (!grown) {
            return;
          }
          list->nodes = grown;
          list->nodes[list->count++] = cur;
        }
      }
      if (cur->children) {
        fpv_html_collect_by_class(cur->children, tag, class_name, list);
      }
    }
  }
}

fpv_html_node_list_t fpv_html_find_all_by_class(
    xmlNode* node,
    const char* tag,
    const char* class_name) {
  fpv_html_node_list_t list;
  list.nodes = NULL;
  list.count = 0;
  if (!node) {
    return list;
  }

  fpv_html_collect_by_class(node, tag, class_name, &list);
  return list;
}

void fpv_html_node_list_destroy(fpv_html_node_list_t* list) {
  if (!list) {
    return;
  }
  free(list->nodes);
  list->nodes = NULL;
  list->count = 0;
}

char* fpv_html_node_text(const xmlNode* node) {
  if (!node) {
    return NULL;
  }
  xmlChar* content = xmlNodeGetContent((xmlNode*)node);
  if (!content) {
    return NULL;
  }
  char* copy = fpv_strdup((const char*)content);
  xmlFree(content);
  return copy;
}

char* fpv_html_node_attr(const xmlNode* node, const char* name) {
  if (!node || !name) {
    return NULL;
  }
  xmlChar* value = xmlGetProp((xmlNode*)node, (const xmlChar*)name);
  if (!value) {
    return NULL;
  }
  char* copy = fpv_strdup((const char*)value);
  xmlFree(value);
  return copy;
}
