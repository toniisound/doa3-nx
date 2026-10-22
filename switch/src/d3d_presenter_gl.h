// SPDX-License-Identifier: GPL-3.0-or-later
// OpenGL backend for the Switch presenter. Every call runs on the render
// thread that owns the EGL context (d3d_presenter_gl_thread.cpp); command
// pointers are valid for the duration of the call, as in d3d_presenter.h.
#ifndef DOA3_NX_D3D_PRESENTER_GL_H
#define DOA3_NX_D3D_PRESENTER_GL_H

#include "d3d_presenter.h"

RecompD3dPresenterError gl_backend_create(
    const RecompD3dPresenterConfig *config, RecompD3dPresenter **presenter);
RecompD3dPresenterError gl_backend_submit(
    RecompD3dPresenter *presenter, const RecompD3dPresenterCommand *command);
RecompD3dPresenterError gl_backend_release_memory(
    RecompD3dPresenter *presenter, uint32_t base, uint32_t size);
RecompD3dPresenterError gl_backend_destroy(RecompD3dPresenter **presenter);
void gl_backend_set_immediate_present(RecompD3dPresenter *presenter, bool enabled);

#endif
