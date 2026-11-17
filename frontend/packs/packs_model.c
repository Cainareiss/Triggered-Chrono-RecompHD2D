/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Asset packs: [[model]] entries' glTF files (packs.h), read with cgltf
 * (third_party/cgltf, MIT): checked when a pack loads, and turned into
 * triangles grouped by material, with their PNG textures decoded, when a
 * map first shows them. The 3D view places and draws them
 * (frontend/pack_scene.c, frontend/hd2d). */
#include "packs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pngio.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* A glTF's buffer and image files are paths a pack names too: they stay in
   the folder of the .gltf (or below). 0 fine, -1 with the reason in why. */
static int uri_ok(const char *gltf, const char *uri, char *why, int whylen)
{
    if (!uri || !strncmp(uri, "data:", 5))
        return 0;
    char dir[4096], rel[4096];
    const char *slash = strrchr(gltf, '/');
    int n = slash ? (int)(slash - gltf) : 0;
    snprintf(dir, sizeof dir, "%.*s", slash ? n : 1, slash ? gltf : ".");
    if (strstr(uri, "://")) {
        snprintf(why, (size_t)whylen, "file on the network");
        return -1;
    }
    snprintf(rel, sizeof rel, "%s", uri);
    cgltf_decode_uri(rel);
    return packs_path_ok(dir, rel, why, whylen);
}

static int gltf_uris_ok(const cgltf_data *d, const char *path, char *err, int errlen)
{
    char why[96];
    for (cgltf_size k = 0; k < d->buffers_count; k++)
        if (uri_ok(path, d->buffers[k].uri, why, sizeof why)) {
            snprintf(err, (size_t)errlen, "%s: buffer \"%s\": %s", path, d->buffers[k].uri, why);
            return -1;
        }
    for (cgltf_size k = 0; k < d->images_count; k++)
        if (uri_ok(path, d->images[k].uri, why, sizeof why)) {
            snprintf(err, (size_t)errlen, "%s: image \"%s\": %s", path, d->images[k].uri, why);
            return -1;
        }
    return 0;
}

int packs_model_parse(const char *path, packs_model *m, char *err, int errlen)
{
    static const char *const why[] = {
        [cgltf_result_success] = "ok",
        [cgltf_result_data_too_short] = "data too short",
        [cgltf_result_unknown_format] = "not glTF or GLB",
        [cgltf_result_invalid_json] = "invalid JSON",
        [cgltf_result_invalid_gltf] = "invalid glTF",
        [cgltf_result_invalid_options] = "invalid options",
        [cgltf_result_file_not_found] = "file not found",
        [cgltf_result_io_error] = "I/O error",
        [cgltf_result_out_of_memory] = "out of memory",
        [cgltf_result_legacy_gltf] = "glTF 1.0 (2.0 needed)",
    };
    cgltf_options opt;
    memset(&opt, 0, sizeof opt);
    cgltf_data *d = NULL;
    cgltf_result r = cgltf_parse_file(&opt, path, &d);
    if (r == cgltf_result_success && gltf_uris_ok(d, path, err, errlen)) {
        cgltf_free(d);
        return -1;
    }
    if (r == cgltf_result_success)
        r = cgltf_load_buffers(&opt, d, path);
    if (r == cgltf_result_success)
        r = cgltf_validate(d);
    if (r != cgltf_result_success) {
        snprintf(err, (size_t)errlen, "%s: %s", path,
                 (unsigned)r < sizeof why / sizeof *why && why[r] ? why[r] : "error");
        cgltf_free(d);
        return -1;
    }
    m->meshes = (int)d->meshes_count;
    m->primitives = 0;
    int have = 0;
    for (cgltf_size i = 0; i < d->meshes_count; i++)
        for (cgltf_size j = 0; j < d->meshes[i].primitives_count; j++) {
            const cgltf_primitive *p = &d->meshes[i].primitives[j];
            m->primitives++;
            for (cgltf_size a = 0; a < p->attributes_count; a++) {
                const cgltf_accessor *acc = p->attributes[a].data;
                if (p->attributes[a].type != cgltf_attribute_type_position || !acc)
                    continue;
                float lo[3], hi[3];
                if (acc->has_min && acc->has_max) {
                    for (int k = 0; k < 3; k++)
                        lo[k] = acc->min[k], hi[k] = acc->max[k];
                } else {   /* no bounds given: read the positions */
                    for (int k = 0; k < 3; k++)
                        lo[k] = 1e30f, hi[k] = -1e30f;
                    for (cgltf_size v = 0; v < acc->count; v++) {
                        float x[3] = {0, 0, 0};
                        cgltf_accessor_read_float(acc, v, x, 3);
                        for (int k = 0; k < 3; k++) {
                            if (x[k] < lo[k]) lo[k] = x[k];
                            if (x[k] > hi[k]) hi[k] = x[k];
                        }
                    }
                }
                for (int k = 0; k < 3; k++) {
                    if (!have || lo[k] < m->min[k]) m->min[k] = lo[k];
                    if (!have || hi[k] > m->max[k]) m->max[k] = hi[k];
                }
                have = 1;
            }
        }
    cgltf_free(d);
    if (!m->primitives) {
        snprintf(err, (size_t)errlen, "%s: no mesh primitives", path);
        return -1;
    }
    return 0;
}

/* ---- meshes: what the 3D view draws ---- */

void packs_mesh_free(packs_mesh *m)
{
    for (int k = 0; k < m->ntex; k++)
        free(m->tex[k].rgba);
    free(m->v);
    free(m->idx);
    free(m->part);
    free(m->tex);
    memset(m, 0, sizeof *m);
}

/* An image's PNG bytes: in a buffer view (GLB), a data: URI, or a file
   beside the glTF. Returns malloc'd RGBA, or NULL with *why. */
static uint8_t *image_rgba(const cgltf_options *opt, const cgltf_image *im, const char *path, int *w,
                           int *h, const char **why)
{
    *why = NULL;
    if (im->buffer_view) {
        const cgltf_buffer_view *bv = im->buffer_view;
        if (!bv->buffer || !bv->buffer->data || bv->offset + bv->size > bv->buffer->size) {
            *why = "image buffer missing";
            return NULL;
        }
        return pngio_decode_rgba((const uint8_t *)bv->buffer->data + bv->offset, bv->size, w, h, why);
    }
    if (!im->uri) {
        *why = "image without data";
        return NULL;
    }
    if (!strncmp(im->uri, "data:", 5)) {
        const char *b64 = strstr(im->uri, ";base64,");
        if (!b64) {
            *why = "data URI not base64";
            return NULL;
        }
        b64 += 8;
        size_t len = strlen(b64), n = len / 4 * 3;
        if (len >= 1 && b64[len - 1] == '=')
            n--;
        if (len >= 2 && b64[len - 2] == '=')
            n--;
        void *data = NULL;
        if (cgltf_load_buffer_base64(opt, n, b64, &data) != cgltf_result_success) {
            *why = "bad base64";
            return NULL;
        }
        uint8_t *rgba = pngio_decode_rgba(data, n, w, h, why);
        free(data);
        return rgba;
    }
    if (strstr(im->uri, "://")) {
        *why = "image on the network: not read";
        return NULL;
    }
    char file[4096];
    const char *slash = strrchr(path, '/');
    int dir = slash ? (int)(slash - path + 1) : 0;
    if (snprintf(file, sizeof file, "%.*s%s", dir, path, im->uri) >= (int)sizeof file) {
        *why = "path too long";
        return NULL;
    }
    cgltf_decode_uri(file + dir);
    return pngio_read_rgba(file, w, h, why);
}

typedef struct {
    packs_mesh *m;
    int capv, capi, capp, capt;
    const cgltf_image *timg[64];   /* image -> m->tex index */
    const cgltf_options *opt;
    const char *path;
    int bad_tex;
} mesh_build;

static int grow_to(void **p, int *cap, int want, size_t size)
{
    if (want <= *cap)
        return 0;
    int c = *cap ? *cap : 64;
    while (c < want)
        c *= 2;
    void *n = realloc(*p, (size_t)c * size);
    if (!n)
        return -1;
    *p = n;
    *cap = c;
    return 0;
}

static int texture_of(mesh_build *b, const cgltf_texture *t)
{
    if (!t || !t->image)
        return -1;
    for (int k = 0; k < b->m->ntex; k++)
        if (b->timg[k] == t->image)
            return b->m->tex[k].rgba ? k : -1;
    if (b->m->ntex >= 64 || grow_to((void **)&b->m->tex, &b->capt, b->m->ntex + 1, sizeof *b->m->tex))
        return -1;
    packs_mtex *x = &b->m->tex[b->m->ntex];
    const char *why = NULL;
    x->rgba = image_rgba(b->opt, t->image, b->path, &x->w, &x->h, &why);
    if (!x->rgba && b->bad_tex++ < 3)
        fprintf(stderr, "packs: %s: texture %s: %s (PNG only)\n", b->path,
                t->image->uri && strncmp(t->image->uri, "data:", 5) ? t->image->uri : "(embedded)",
                why ? why : "unreadable");
    b->timg[b->m->ntex] = t->image;
    return x->rgba ? b->m->ntex++ : (b->m->ntex++, -1);
}

static int add_primitive(mesh_build *b, const cgltf_primitive *p, const float *M)
{
    if (p->type != cgltf_primitive_type_triangles)
        return 0;
    const cgltf_accessor *pos = NULL, *nrm = NULL, *uv = NULL, *col = NULL;
    const cgltf_material *mat = p->material;
    int uv_set = 0;
    if (mat && mat->has_pbr_metallic_roughness)
        uv_set = mat->pbr_metallic_roughness.base_color_texture.texcoord;
    for (cgltf_size a = 0; a < p->attributes_count; a++) {
        const cgltf_attribute *at = &p->attributes[a];
        if (at->type == cgltf_attribute_type_position)
            pos = at->data;
        else if (at->type == cgltf_attribute_type_normal)
            nrm = at->data;
        else if (at->type == cgltf_attribute_type_texcoord && at->index == uv_set)
            uv = at->data;
        else if (at->type == cgltf_attribute_type_color && at->index == 0)
            col = at->data;
    }
    if (!pos || !pos->count)
        return 0;
    packs_mesh *m = b->m;
    int nv = (int)pos->count, ni = p->indices ? (int)p->indices->count : nv;
    ni -= ni % 3;
    if (nv > 1 << 22 || ni > 1 << 24 || m->nv + nv > 1 << 24 ||
        grow_to((void **)&m->v, &b->capv, m->nv + nv, sizeof *m->v) ||
        grow_to((void **)&m->idx, &b->capi, m->ni + ni, sizeof *m->idx) ||
        grow_to((void **)&m->part, &b->capp, m->nparts + 1, sizeof *m->part))
        return -1;
    /* Normals go through the inverse transpose: the cofactors of M's 3x3. */
    float N[9];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            int r1 = (r + 1) % 3, r2 = (r + 2) % 3, c1 = (c + 1) % 3, c2 = (c + 2) % 3;
            N[c * 3 + r] = M[c1 * 4 + r1] * M[c2 * 4 + r2] - M[c2 * 4 + r1] * M[c1 * 4 + r2];
        }
    packs_mvertex *v = m->v + m->nv;
    for (int k = 0; k < nv; k++) {
        float x[4] = {0, 0, 0, 1}, n[3] = {0, 0, 0}, t[2] = {0, 0};
        cgltf_accessor_read_float(pos, (cgltf_size)k, x, 3);
        for (int r = 0; r < 3; r++)
            v[k].pos[r] = M[r] * x[0] + M[4 + r] * x[1] + M[8 + r] * x[2] + M[12 + r];
        if (nrm && cgltf_accessor_read_float(nrm, (cgltf_size)k, n, 3)) {
            float o[3], l = 0;
            for (int r = 0; r < 3; r++) {
                o[r] = N[r] * n[0] + N[3 + r] * n[1] + N[6 + r] * n[2];
                l += o[r] * o[r];
            }
            l = l > 1e-20f ? 1.0f / sqrtf(l) : 0.0f;
            for (int r = 0; r < 3; r++)
                v[k].nrm[r] = o[r] * l;
        } else {
            v[k].nrm[0] = v[k].nrm[1] = v[k].nrm[2] = 0;   /* worked out below */
        }
        if (uv)
            cgltf_accessor_read_float(uv, (cgltf_size)k, t, 2);
        v[k].uv[0] = t[0];
        v[k].uv[1] = t[1];
        float c[4] = {1, 1, 1, 1};
        if (col)
            cgltf_accessor_read_float(col, (cgltf_size)k, c, cgltf_num_components(col->type));
        for (int r = 0; r < 4; r++)
            v[k].rgba[r] = (uint8_t)(c[r] <= 0 ? 0 : c[r] >= 1 ? 255 : c[r] * 255.0f + 0.5f);
    }
    uint32_t *idx = m->idx + m->ni;
    for (int k = 0; k < ni; k++) {
        size_t i = p->indices ? cgltf_accessor_read_index(p->indices, (cgltf_size)k) : (size_t)k;
        idx[k] = (uint32_t)(m->nv + (int)(i < (size_t)nv ? i : 0));
    }
    if (!nrm) {   /* no normals given: each vertex's faces, area-weighted */
        for (int k = 0; k + 2 < ni; k += 3) {
            packs_mvertex *a = &m->v[idx[k]], *bb = &m->v[idx[k + 1]], *c = &m->v[idx[k + 2]];
            float e1[3], e2[3];
            for (int r = 0; r < 3; r++)
                e1[r] = bb->pos[r] - a->pos[r], e2[r] = c->pos[r] - a->pos[r];
            float f[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                          e1[0] * e2[1] - e1[1] * e2[0]};
            for (int r = 0; r < 3; r++)
                a->nrm[r] += f[r], bb->nrm[r] += f[r], c->nrm[r] += f[r];
        }
        for (int k = 0; k < nv; k++) {
            float *n = v[k].nrm, l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            for (int r = 0; r < 3; r++)
                n[r] = l > 1e-20f ? n[r] / l : r == 1;
        }
    }
    packs_mpart *pt = &m->part[m->nparts++];
    pt->first = m->ni;
    pt->count = ni;
    pt->tex = -1;
    pt->cutout = 0;
    for (int r = 0; r < 4; r++)
        pt->color[r] = 1;
    if (mat) {
        if (mat->has_pbr_metallic_roughness) {
            for (int r = 0; r < 4; r++)
                pt->color[r] = mat->pbr_metallic_roughness.base_color_factor[r];
            pt->tex = texture_of(b, mat->pbr_metallic_roughness.base_color_texture.texture);
        }
        pt->cutout = mat->alpha_mode != cgltf_alpha_mode_opaque;
    }
    m->nv += nv;
    m->ni += ni;
    return 0;
}

static int add_node(mesh_build *b, const cgltf_node *n, int depth)
{
    if (depth > 64)
        return 0;
    if (n->mesh) {
        float M[16];
        cgltf_node_transform_world(n, M);
        for (cgltf_size j = 0; j < n->mesh->primitives_count; j++)
            if (add_primitive(b, &n->mesh->primitives[j], M))
                return -1;
    }
    for (cgltf_size c = 0; c < n->children_count; c++)
        if (add_node(b, n->children[c], depth + 1))
            return -1;
    return 0;
}

int packs_mesh_load(const char *path, packs_mesh *out, char *err, int errlen)
{
    memset(out, 0, sizeof *out);
    cgltf_options opt;
    memset(&opt, 0, sizeof opt);
    cgltf_data *d = NULL;
    cgltf_result r = cgltf_parse_file(&opt, path, &d);
    if (r == cgltf_result_success && gltf_uris_ok(d, path, err, errlen)) {
        cgltf_free(d);
        return -1;
    }
    if (r == cgltf_result_success)
        r = cgltf_load_buffers(&opt, d, path);
    if (r == cgltf_result_success)
        r = cgltf_validate(d);
    if (r != cgltf_result_success) {
        snprintf(err, (size_t)errlen, "%s: cannot read (cgltf error %d)", path, (int)r);
        cgltf_free(d);
        return -1;
    }
    mesh_build b;
    memset(&b, 0, sizeof b);
    b.m = out;
    b.opt = &opt;
    b.path = path;
    int bad = 0;
    if (d->scene) {   /* the default scene's nodes */
        for (cgltf_size k = 0; k < d->scene->nodes_count && !bad; k++)
            bad = add_node(&b, d->scene->nodes[k], 0);
    } else {          /* else every root node */
        for (cgltf_size k = 0; k < d->nodes_count && !bad; k++)
            if (!d->nodes[k].parent)
                bad = add_node(&b, &d->nodes[k], 0);
    }
    cgltf_free(d);
    if (bad || !out->ni) {
        snprintf(err, (size_t)errlen, "%s: %s", path, bad ? "out of memory or too large"
                                                          : "no triangles");
        packs_mesh_free(out);
        return -1;
    }
    return 0;
}
