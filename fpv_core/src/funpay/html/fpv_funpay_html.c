#include "funpay/core/fpv_funpay_internal.h"


#include <stdlib.h>
#include <string.h>

xmlNode* fpv_funpay_find_first_tag(xmlNode* node, const char* tag) {
  for (xmlNode* cur = node; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (cur->name && strcmp((const char*)cur->name, tag) == 0) {
        return cur;
      }
      if (cur->children) {
        xmlNode* found = fpv_funpay_find_first_tag(cur->children, tag);
        if (found) {
          return found;
        }
      }
    }
  }
  return NULL;
}


static void fpv_funpay_collect_tag(
    xmlNode* node,
    const char* tag,
    fpv_html_node_list_t* list) {
  for (xmlNode* cur = node; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (cur->name && strcmp((const char*)cur->name, tag) == 0) {
        xmlNode** grown = (xmlNode**)realloc(
            list->nodes,
            (list->count + 1) * sizeof(*grown));
        if (!grown) {
          return;
        }
        list->nodes = grown;
        list->nodes[list->count++] = cur;
      }
      if (cur->children) {
        fpv_funpay_collect_tag(cur->children, tag, list);
      }
    }
  }
}


fpv_html_node_list_t fpv_funpay_find_all_tag(
    xmlNode* node,
    const char* tag) {
  fpv_html_node_list_t list;
  list.nodes = NULL;
  list.count = 0;
  if (!node || !tag) {
    return list;
  }
  fpv_funpay_collect_tag(node, tag, &list);
  return list;
}


xmlNode* fpv_funpay_find_first_by_attr(
    xmlNode* node,
    const char* tag,
    const char* attr,
    const char* value) {
  for (xmlNode* cur = node; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (!tag || (cur->name && strcmp((const char*)cur->name, tag) == 0)) {
        xmlChar* attr_value = xmlGetProp(cur, (const xmlChar*)attr);
        if (attr_value) {
          bool match = true;
          if (value && strcmp((const char*)attr_value, value) != 0) {
            match = false;
          }
          xmlFree(attr_value);
          if (match) {
            return cur;
          }
        }
      }
      if (cur->children) {
        xmlNode* found =
            fpv_funpay_find_first_by_attr(cur->children, tag, attr, value);
        if (found) {
          return found;
        }
      }
    }
  }
  return NULL;
}


bool fpv_funpay_parse_uint64_attr(
    xmlNode* node,
    const char* attr,
    uint64_t* out_value) {
  if (!node || !attr || !out_value) {
    return false;
  }
  char* value = fpv_html_node_attr(node, attr);
  if (!value || !value[0]) {
    fpv_free(value);
    return false;
  }
  char* end = NULL;
  unsigned long long parsed = strtoull(value, &end, 10);
  bool ok = end && *end == '\0';
  fpv_free(value);
  if (!ok) {
    return false;
  }
  *out_value = (uint64_t)parsed;
  return true;
}


bool fpv_funpay_parse_double_attr(
    xmlNode* node,
    const char* attr,
    double* out_value) {
  if (!node || !attr || !out_value) {
    return false;
  }
  char* value = fpv_html_node_attr(node, attr);
  if (!value || !value[0]) {
    fpv_free(value);
    return false;
  }
  char* end = NULL;
  double parsed = strtod(value, &end);
  bool ok = end && *end == '\0';
  fpv_free(value);
  if (!ok) {
    return false;
  }
  *out_value = parsed;
  return true;
}


static void fpv_funpay_collect_by_attrs(
    xmlNode* node,
    const char* tag,
    const char* const* attrs,
    size_t attr_count,
    fpv_html_node_list_t* list) {
  if (!node || !attrs || attr_count == 0 || !list) {
    return;
  }
  for (xmlNode* cur = node; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (!tag || (cur->name && strcmp((const char*)cur->name, tag) == 0)) {
        bool match = false;
        for (size_t i = 0; i < attr_count; i++) {
          if (!attrs[i]) {
            continue;
          }
          xmlChar* value = xmlGetProp(cur, (const xmlChar*)attrs[i]);
          if (value) {
            xmlFree(value);
            match = true;
            break;
          }
        }
        if (match) {
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
        fpv_funpay_collect_by_attrs(
            cur->children,
            tag,
            attrs,
            attr_count,
            list);
      }
    }
  }
}


fpv_html_node_list_t fpv_funpay_find_all_by_attrs(
    xmlNode* node,
    const char* tag,
    const char* const* attrs,
    size_t attr_count) {
  fpv_html_node_list_t list;
  list.nodes = NULL;
  list.count = 0;
  if (!node || !attrs || attr_count == 0) {
    return list;
  }
  fpv_funpay_collect_by_attrs(node, tag, attrs, attr_count, &list);
  return list;
}


xmlNode* fpv_funpay_find_first_by_class_substr(
    xmlNode* node,
    const char* tag,
    const char* class_substr) {
  if (!node || !class_substr || !class_substr[0]) {
    return NULL;
  }
  for (xmlNode* cur = node; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (!tag || (cur->name && strcmp((const char*)cur->name, tag) == 0)) {
        char* classes = fpv_html_node_attr(cur, "class");
        if (classes) {
          bool match = strstr(classes, class_substr) != NULL;
          fpv_free(classes);
          if (match) {
            return cur;
          }
        }
      }
      if (cur->children) {
        xmlNode* found =
            fpv_funpay_find_first_by_class_substr(cur->children, tag, class_substr);
        if (found) {
          return found;
        }
      }
    }
  }
  return NULL;
}


char* fpv_funpay_find_first_attr_value(
    xmlNode* node,
    const char* tag,
    const char* attr_name) {
  if (!node || !attr_name || !attr_name[0]) {
    return NULL;
  }
  for (xmlNode* cur = node; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (!tag || (cur->name && strcmp((const char*)cur->name, tag) == 0)) {
        char* value = fpv_html_node_attr(cur, attr_name);
        if (value && value[0]) {
          return value;
        }
        fpv_free(value);
      }
      if (cur->children) {
        char* nested =
            fpv_funpay_find_first_attr_value(cur->children, tag, attr_name);
        if (nested) {
          return nested;
        }
      }
    }
  }
  return NULL;
}
