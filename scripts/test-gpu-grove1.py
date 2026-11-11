"""Focused source-boundary regression checks for the Grove sky GPU path.

The hardware screenshots/logs in the QA report establish visual behavior;
these checks pin the software sky mask and world/UI ordering that make it safe.
"""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
STAR = (ROOT / 'SourceSnapshot/src/GameSrc/star.c').read_text()
FRAME = (ROOT / 'SourceSnapshot/src/GameSrc/frmain.c').read_text()
SETUP = (ROOT / 'SourceSnapshot/src/GameSrc/frsetup.c').read_text()
GPU = (ROOT / 'SourceSnapshot/src/MacSrc/GpuWorld.c').read_text()


class GroveSkyContract(unittest.TestCase):
    def test_original_sky_polygon_and_open_ceiling_remain(self):
        sky = STAR[STAR.index('void star_poly('):STAR.index('void star_empty(')]
        self.assertIn('g3_draw_poly(0xff, n, vp)', sky)
        self.assertIn('star_empty(n, vp)', sky)
        self.assertNotIn('gpu_world_reject("sky/stars")', STAR)

    def test_solid_geometry_cannot_admit_a_star(self):
        ordinary = STAR[STAR.index('void star_render('):]
        for x, y in (('x', 'y'), ('x1', 'y1')):
            guard = rf'if\s*\(gr_get_pixel\({x},\s*{y}\)\s*==\s*0xff\)\s*\{{'
            self.assertRegex(ordinary, guard)
            block = ordinary[re.search(guard, ordinary).end():]
            self.assertLess(block.index('gpu_world_star_pixel('), block.index('}', 0) + 1)
        aa_start = STAR.rindex('void do_aa_star_pixel(')
        aa = STAR[aa_start:STAR.index('void do_aa_star(', aa_start)]
        self.assertRegex(aa, r'if\s*\(gr_get_pixel\(x,\s*y\)\s*==\s*0xff\)')
        self.assertLess(aa.index('gr_set_pixel('), aa.index('gpu_world_star_pixel('))

    def test_capture_precedes_gpu_end_and_software_view_send(self):
        self.assertLess(FRAME.index('fr_pipe_end();'), FRAME.index('fr_render_stars();'))
        self.assertLess(FRAME.index('fr_render_stars();'), FRAME.index('gpu_world_end(gpu_previous);'))
        self.assertLess(FRAME.index('gpu_world_end(gpu_previous);'), FRAME.index('fr_send_view();'))
        render = SETUP[SETUP.index('void fr_render_stars('):SETUP.index('int fr_send_view(')]
        self.assertIn('star_render();', render)

    def test_star_palette_and_bounded_point_submission(self):
        self.assertIn('MAX_STAR_PIXELS=16384', GPU)
        self.assertIn('texelFetch(pal,ivec2(index,0),0)', GPU)
        self.assertIn('gpu_world_reject("star pixel capacity")', GPU)
        submit = GPU[GPU.index('static int submit('):GPU.index('void gpu_world_end(')]
        self.assertLess(submit.index('glDrawArrays(GL_TRIANGLES,d->first,d->count)'),
                        submit.index('glDrawArrays(GL_POINTS,0,nstar_pixels)'))

    def test_special_fallback_and_world_only_post_remain(self):
        begin = GPU[GPU.index('int gpu_world_begin('):GPU.index('void gpu_world_end(')]
        for guard in ('FR_SFX_MASK', 'UseCutscenePalette', 'curr_clut_table',
                      'global_fullmap->cyber', 'secret_render_fx'):
            self.assertIn(guard, begin)
        end = GPU[GPU.index('void gpu_world_end('):GPU.index('int gpu_world_compose(')]
        self.assertIn('if(failed){retained=0;log_fallback(', end)
        self.assertIn('software_world+y*view_w', end)
        self.assertIn('SDL_SetRenderTarget(renderer,world)', end)
        compose = GPU[GPU.index('int gpu_world_compose('):]
        self.assertLess(compose.index('post_process()'), compose.index('SDL_RenderCopy(renderer,composed'))


if __name__ == '__main__':
    unittest.main()
