/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Asset packs: [[model]] entries' glTF files (packs.h), read with cgltf
 * (third_party/cgltf, MIT). Load and check only: the 3D path
 * (frontend/hd2d/scene3d, through s3d_set_cell_hook) draws them later. */
#include "packs.h"

#include <stdio.h>
#include <string.h>

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
