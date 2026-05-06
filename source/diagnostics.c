#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diagnostics.h"

static size_t armlet_strlen0(const char *s) { return s ? strlen(s) : 0; }

static size_t armlet_min_size(size_t a, size_t b) { return a < b ? a : b; }
static size_t armlet_max_size(size_t a, size_t b) { return a > b ? a : b; }

static void armlet_find_line_at(const char *data, size_t len, size_t pos,
                                size_t *out_line_start, size_t *out_line_end,
                                size_t *out_lineno) {
  if (!data) {
    if (out_line_start)
      *out_line_start = 0;
    if (out_line_end)
      *out_line_end = 0;
    if (out_lineno)
      *out_lineno = 1;
    return;
  }

  pos = armlet_min_size(pos, len);

  size_t line_start = 0;
  size_t lineno = 1;

  for (size_t i = 0; i < pos; ++i) {
    if (data[i] == '\n') {
      lineno++;
      line_start = i + 1;
    }
  }

  size_t line_end = line_start;
  while (line_end < len && data[line_end] != '\n')
    line_end++;

  if (out_line_start)
    *out_line_start = line_start;
  if (out_line_end)
    *out_line_end = line_end;
  if (out_lineno)
    *out_lineno = lineno;
}

static char *armlet_dup_range(const char *data, size_t start, size_t end) {
  if (data == NULL || end < start) {
    return NULL;
  }
  const size_t n = end - start;
  char *s = (char *)CHECKED_MALLOC(n + 1);
  memcpy(s, data + start, n);
  s[n] = '\0';
  return s;
}

static void armlet_line_info_free(armlet_line_info *li) {
  if (!li) {
    return;
  }

  free(li->line);
  li->line = NULL;
}

static void armlet_line_infos_free(armlet_line_info *lis, size_t n) {
  if (!lis) {
    return;
  }

  for (size_t i = 0; i < n; ++i) {
    armlet_line_info_free(&lis[i]);
  }

  free(lis);
}

armlet_line_info armlet_source_line_info_one(const armlet_source *src,
                                             armlet_span span) {
  armlet_line_info out = {0};
  if (src == NULL || src->data == NULL)
    return out;

  const char *data = src->data;
  const size_t len = armlet_strlen0(data);

  size_t start = (size_t)span.start;
  size_t end = (size_t)span.end;
  start = armlet_min_size(start, len);
  end = armlet_min_size(end, len);
  if (end < start)
    end = start;

  if (start == end && start < len && data[start] == '\n' && start > 0) {
    size_t line_start = 0, line_end = 0, lineno = 1;
    armlet_find_line_at(data, len, start - 1, &line_start, &line_end, &lineno);
    out.line = armlet_dup_range(data, line_start, line_end);
    out.lineno = lineno;
    const size_t line_len = line_end - line_start;
    out.col_start = line_len + 1;
    out.col_end = line_len + 1;
    return out;
  }

  size_t line_start = 0, line_end = 0, lineno = 1;
  armlet_find_line_at(data, len, start, &line_start, &line_end, &lineno);

  out.line = armlet_dup_range(data, line_start, line_end);
  out.lineno = lineno;

  out.col_start = (start - line_start) + 1;

  if (end > line_end) {
    out.col_end = (line_end - line_start) + 1;
  } else {
    out.col_end = (end - line_start) + 1;
  }

  if (start == end) {
    out.col_end = out.col_start;
  }

  return out;
}

static int armlet_source_line_infos_from_span(const armlet_source *src,
                                              armlet_span span,
                                              armlet_line_info **out_infos,
                                              size_t *out_count) {
  if (out_infos)
    *out_infos = NULL;
  if (out_count)
    *out_count = 0;

  if (!src || !src->data || !out_infos || !out_count)
    return 0;

  const char *data = src->data;
  const size_t len = armlet_strlen0(data);

  size_t start = armlet_min_size((size_t)span.start, len);
  size_t end = armlet_min_size((size_t)span.end, len);
  if (end < start)
    end = start;

  if (start == end && start < len && data[start] == '\n' && start > 0) {
    armlet_line_info *arr = (armlet_line_info *)calloc(1, sizeof(*arr));
    if (!arr)
      return 1;
    arr[0] = armlet_source_line_info_one(src, span);
    if (!arr[0].line) {
      free(arr);
      return 1;
    }
    *out_infos = arr;
    *out_count = 1;
    return 0;
  }

  size_t count = 1;
  for (size_t i = start; i < end && i < len; ++i) {
    if (data[i] == '\n')
      count++;
  }

  armlet_line_info *infos = (armlet_line_info *)calloc(count, sizeof(*infos));
  if (!infos)
    return 1;

  size_t cur = start;
  size_t idx = 0;

  while (true) {
    size_t line_start = 0, line_end = 0, lineno = 1;
    armlet_find_line_at(data, len, cur, &line_start, &line_end, &lineno);

    size_t seg_start = cur;
    size_t seg_end = armlet_min_size(end, line_end);

    armlet_line_info li = {0};
    li.line = armlet_dup_range(data, line_start, line_end);
    if (!li.line) {
      armlet_line_infos_free(infos, idx);
      return 1;
    }

    li.lineno = lineno;

    if (idx == 0) {
      li.col_start = (seg_start - line_start) + 1;
    } else {
      li.col_start = 1;
    }

    if (end > line_end) {
      li.col_end = (line_end - line_start) + 1;
    } else {
      li.col_end = (seg_end - line_start) + 1;
    }

    if (start == end)
      li.col_end = li.col_start;

    infos[idx++] = li;

    if (seg_end >= end)
      break;

    cur = line_end;
    if (cur < len && data[cur] == '\n')
      cur++;
    if (cur > end)
      break;
  }

  *out_infos = infos;
  *out_count = idx;
  return 0;
}

static int armlet_visualize_line_tabs(const char *line, size_t tabstop,
                                      char **out_visual_line,
                                      size_t **out_map) {
  if (out_visual_line)
    *out_visual_line = NULL;
  if (out_map)
    *out_map = NULL;
  if (!line || !out_visual_line || !out_map || tabstop == 0)
    return 1;

  const size_t n = strlen(line);

  size_t *map = (size_t *)CHECKED_MALLOC((n + 1) * sizeof(*map));

  size_t vis_len = 0;
  size_t vis_col = 1;
  for (size_t i = 0; i < n; ++i) {
    map[i] = vis_col;
    if (line[i] == '\t') {
      size_t col0 = vis_col - 1;
      size_t spaces = tabstop - (col0 % tabstop);
      vis_len += spaces;
      vis_col += spaces;
    } else {
      vis_len += 1;
      vis_col += 1;
    }
  }
  map[n] = vis_col;

  char *vis = (char *)CHECKED_MALLOC(vis_len + 1);

  size_t j = 0;
  vis_col = 1;
  for (size_t i = 0; i < n; ++i) {
    if (line[i] == '\t') {
      size_t col0 = vis_col - 1;
      size_t spaces = tabstop - (col0 % tabstop);
      for (size_t k = 0; k < spaces; ++k)
        vis[j++] = ' ';
      vis_col += spaces;
    } else {
      vis[j++] = line[i];
      vis_col += 1;
    }
  }
  vis[j] = '\0';

  *out_visual_line = vis;
  *out_map = map;
  return 0;
}

static void armlet_print_repeat(FILE *out, char c, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    fputc((int)c, out);
  }
}

void armlet_source_diagnostic(FILE *out, const armlet_source *src,
                              armlet_span span, bool colored, const char *kind,
                              const char *message) {
  if (out == NULL) {
    out = stderr;
  }

  if (src == NULL || src->data == NULL) {
    return;
  }

  armlet_line_info *lines = NULL;
  size_t nlines = 0;
  if (armlet_source_line_infos_from_span(src, span, &lines, &nlines) != 0 ||
      nlines == 0) {
    return;
  }

  const char *k_kind = kind ? kind : (colored ? "\e[1;31mERROR\e[0m" : "ERROR");

  const size_t header_col = lines[0].col_start;
  const size_t header_col_end = lines[0].col_end;
  fprintf(out, "%s:%zu:%zu:%zu: %s: %s\n", src->file ? src->file : "<input>",
          lines[0].lineno, header_col, header_col_end, k_kind,
          message ? message : "");

  size_t max_lineno = lines[nlines - 1].lineno;
  size_t width = 1;
  for (size_t t = max_lineno; t >= 10; t /= 10) {
    width++;
  }

  const size_t tabstop = 8;

  for (size_t i = 0; i < nlines; ++i) {
    armlet_line_info *li = &lines[i];
    if (!li->line) {
      continue;
    }

    char *vis_line = NULL;
    size_t *map = NULL;
    if (armlet_visualize_line_tabs(li->line, tabstop, &vis_line, &map) != 0) {
      vis_line = strdup(li->line);
      map = NULL;
    }

    fprintf(out, "%*zu | %s\n", (int)width, li->lineno,
            vis_line ? vis_line : "");
    fprintf(out, "%*s | ", (int)width, "");

    if (vis_line != NULL) {
      free(vis_line);
    }

    size_t line_len_bytes = strlen(li->line);
    size_t bstart = li->col_start;
    size_t bend = li->col_end;

    bstart =
        armlet_max_size((size_t)1, armlet_min_size(bstart, line_len_bytes + 1));
    bend =
        armlet_max_size((size_t)1, armlet_min_size(bend, line_len_bytes + 1));

    size_t vstart = bstart;
    size_t vend = bend;

    if (map != NULL) {
      size_t bi_start = (bstart - 1);
      size_t bi_end = (bend - 1);

      if (bi_start > line_len_bytes) {
        bi_start = line_len_bytes;
      }

      if (bi_end > line_len_bytes) {
        bi_end = line_len_bytes;
      }

      vstart = map[bi_start];
      vend = map[bi_end];

      free(map);
    }

    if (vstart == vend) {
      if (vstart > 1) {
        armlet_print_repeat(out, ' ', vstart - 1);
      }

      fputc('^', out);
      fputc('\n', out);
    } else {
      size_t left = (vstart > 1) ? (vstart - 1) : 0;
      size_t width_mark = (vend > vstart) ? (vend - vstart) : 1;
      armlet_print_repeat(out, ' ', left);
      fputc('^', out);
      if (width_mark > 2) {
        armlet_print_repeat(out, '~', width_mark - 2);
      }
      fputc('^', out);
      fputc('\n', out);
    }
  }

  armlet_line_infos_free(lines, nlines);
}
