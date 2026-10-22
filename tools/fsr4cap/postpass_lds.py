#!/usr/bin/env python3
# kzsf: rewrites the FSR 4.1.1 postpass (spirv-dis text of dxil-spirv output) so that its image
# stores go through workgroup memory, like tools/fsr4_post_lds.pl does for FSR 4 v07.
#
#   postpass_lds.py < postpass.spvasm > postpass_lds.spvasm   (then spirv-as --target-env spv1.3)
#
# Each invocation computes a 2x2 block of output pixels (the workgroup: 32x32) and writes it into
# three images (recurrent state, history, output) one pixel per store; on RDNA3 those strided
# stores cost most of the pass. Here every store saves its value in workgroup memory (10 floats
# per pixel: history.xyz, output.xyz, recurrent.xyzw; history and output repeat x in alpha), and
# after the pass's bounds check the workgroup writes its 32x32 block in contiguous rows. The
# rest of the module is untouched; values stay float and the stores convert them as before, so
# the result is bit-exact (tools/fsr4cap/verify.sh).
import re
import sys

lines = sys.stdin.read().split('\n')
out = []

def find(pattern, start=0):
    for i in range(start, len(lines)):
        m = re.search(pattern, lines[i])
        if m:
            return i, m
    sys.exit(f'postpass_lds: no match for {pattern}')

# Entry, ids the shader already computes: workgroup id, local id, the thread bounds (W/2, H/2).
main_i, _ = find(r'^\s*%main = OpFunction ')
sel_i, sel = find(r'OpSelectionMerge (%\w+) None', main_i)
merge = sel[1]
_, cond = find(r'OpBranchConditional (%\w+) ' + re.escape(merge) + r' (%\w+)', sel_i)
_, lor = find(r'^\s*' + re.escape(cond[1]) + r' = OpLogicalOr %bool (%\w+) (%\w+)', main_i)
_, gx = find(r'^\s*' + re.escape(lor[1]) + r' = OpUGreaterThanEqual %bool (%\w+) (%\w+)', main_i)
_, gy = find(r'^\s*' + re.escape(lor[2]) + r' = OpUGreaterThanEqual %bool (%\w+) (%\w+)', main_i)
w2, h2 = gx[2], gy[2]

# Image loads feeding the stores: variable -> image type.
image_type = {}
for line in lines[main_i:]:
    m = re.match(r'\s*(%\w+) = OpLoad (%\w+) (%rw_\w+)$', line)
    if m:
        image_type[m[3]] = m[2]
for name in ('%rw_history_color', '%rw_mlsr_output_color', '%rw_recurrent_0'):
    if name not in image_type:
        sys.exit(f'postpass_lds: no store to {name}')

defs = {}
for line in lines:
    m = re.match(r'\s*(%\w+) = (.*)$', line)
    if m:
        defs[m[1]] = m[2]

SLOT = {'%rw_history_color': 0, '%rw_mlsr_output_color': 3, '%rw_recurrent_0': 6}
n = [0]

def new(prefix):
    n[0] += 1
    return f'%kzsf_{prefix}{n[0]}'

decl = [
    '%kzsf_u5 = OpConstant %uint 5', '%kzsf_u31 = OpConstant %uint 31', '%kzsf_u32 = OpConstant %uint 32',
    '%kzsf_u10 = OpConstant %uint 10', '%kzsf_u256 = OpConstant %uint 256', '%kzsf_u2 = OpConstant %uint 2',
    '%kzsf_u264 = OpConstant %uint 264', '%kzsf_u10240 = OpConstant %uint 10240',
] + [f'%kzsf_c{c} = OpConstant %uint {c}' for c in range(10)] + [
    '%kzsf_arr = OpTypeArray %float %kzsf_u10240',
    '%kzsf_ptr_arr = OpTypePointer Workgroup %kzsf_arr',
    '%kzsf_ptr_f = OpTypePointer Workgroup %float',
    '%kzsf_lds = OpVariable %kzsf_ptr_arr Workgroup',
]
preamble = [
    '%kzsf_wgx_p = OpAccessChain %_ptr_Input_uint %gl_WorkGroupID %uint_0',
    '%kzsf_wgx = OpLoad %uint %kzsf_wgx_p',
    '%kzsf_wgy_p = OpAccessChain %_ptr_Input_uint %gl_WorkGroupID %uint_1',
    '%kzsf_wgy = OpLoad %uint %kzsf_wgy_p',
    '%kzsf_lid_p = OpAccessChain %_ptr_Input_uint %gl_LocalInvocationID %uint_0',
    '%kzsf_lid = OpLoad %uint %kzsf_lid_p',
    '%kzsf_x0 = OpShiftLeftLogical %uint %kzsf_wgx %kzsf_u5',
    '%kzsf_y0 = OpShiftLeftLogical %uint %kzsf_wgy %kzsf_u5',
]

def flush():
    code = ['OpControlBarrier %kzsf_u2 %kzsf_u2 %kzsf_u264']
    for k in range(4):
        i, lx, ly, px, py = new('i'), new('lx'), new('ly'), new('px'), new('py')
        kk = new('k')
        code += [
            f'{kk} = OpConstant %uint {256 * k}' if False else '',
            f'{i} = OpIAdd %uint %kzsf_lid %kzsf_k{k}',
            f'{lx} = OpBitwiseAnd %uint {i} %kzsf_u31',
            f'{ly} = OpShiftRightLogical %uint {i} %kzsf_u5',
            f'{px} = OpIAdd %uint %kzsf_x0 {lx}',
            f'{py} = OpIAdd %uint %kzsf_y0 {ly}',
        ]
        tx, ty, cx, cy, c = new('tx'), new('ty'), new('cx'), new('cy'), new('c')
        code += [
            f'{tx} = OpShiftRightLogical %uint {px} %uint_1',
            f'{ty} = OpShiftRightLogical %uint {py} %uint_1',
            f'{cx} = OpULessThan %bool {tx} {w2}',
            f'{cy} = OpULessThan %bool {ty} {h2}',
            f'{c} = OpLogicalAnd %bool {cx} {cy}',
        ]
        then, done = f'%kzsf_then{k}', f'%kzsf_done{k}'
        code += [f'OpSelectionMerge {done} None', f'OpBranchConditional {c} {then} {done}', f'{then} = OpLabel']
        base = new('base')
        code.append(f'{base} = OpIMul %uint {i} %kzsf_u10')
        vals = []
        for comp in range(10):
            e, p, v = new('e'), new('p'), new('v')
            code += [f'{e} = OpIAdd %uint {base} %kzsf_c{comp}', f'{p} = OpAccessChain %kzsf_ptr_f %kzsf_lds {e}',
                     f'{v} = OpLoad %float {p}']
            vals.append(v)
        coord = new('coord')
        code.append(f'{coord} = OpCompositeConstruct %v2uint {px} {py}')
        texels = {
            '%rw_history_color': [vals[0], vals[1], vals[2], vals[0]],
            '%rw_mlsr_output_color': [vals[3], vals[4], vals[5], vals[3]],
            '%rw_recurrent_0': vals[6:10],
        }
        for name, comps in texels.items():
            img, tex = new('img'), new('tex')
            code += [f'{img} = OpLoad {image_type[name]} {name}',
                     f'{tex} = OpCompositeConstruct %v4float ' + ' '.join(comps),
                     f'OpImageWrite {img} {coord} {tex}']
        code += [f'OpBranch {done}', f'{done} = OpLabel']
    return [c for c in code if c]

decl += [f'%kzsf_k{k} = OpConstant %uint {256 * k}' for k in range(4)]

stores = 0
in_main = False
merge_label_seen = False
for idx, line in enumerate(lines):
    s = line.strip()
    if s.startswith('%main = OpFunction'):
        out += ['               ' + d for d in decl]
        in_main = True
        out.append(line)
        continue
    if in_main and re.match(r'%\w+ = OpLabel$', s) and not merge_label_seen and preamble:
        out.append(line)
        out += ['               ' + p for p in preamble]
        preamble = []
        continue
    m = re.match(r'OpImageWrite (%\w+) (%\w+) (%\w+)$', s)
    if in_main and m:
        img_def = defs[m[1]]
        var = img_def.split()[-1]
        coord = re.match(r'OpCompositeConstruct %v2uint (%\w+) (%\w+)$', defs[m[2]])
        if var not in SLOT or not coord:
            sys.exit(f'postpass_lds: unexpected store {s}')
        x, y = coord[1], coord[2]
        texel = defs[m[3]]
        if var == '%rw_recurrent_0':
            if not texel.startswith('OpFConvert %v4float'):
                sys.exit(f'postpass_lds: recurrent texel {texel}')
            comps = []
            for c in range(4):
                v = new('r')
                out.append(f'               {v} = OpCompositeExtract %float {m[3]} {c}')
                comps.append(v)
        else:
            t = re.match(r'OpCompositeConstruct %v4float (%\w+) (%\w+) (%\w+) (%\w+)$', texel)
            if not t or t[4] != t[1]:
                sys.exit(f'postpass_lds: texel of {var} is not (x, y, z, x): {texel}')
            comps = [t[1], t[2], t[3]]
        lx, ly, row, pix, base = new('slx'), new('sly'), new('srow'), new('spix'), new('sbase')
        out += ['               ' + c for c in [
            f'{lx} = OpISub %uint {x} %kzsf_x0', f'{ly} = OpISub %uint {y} %kzsf_y0',
            f'{row} = OpShiftLeftLogical %uint {ly} %kzsf_u5', f'{pix} = OpIAdd %uint {row} {lx}',
            f'{base} = OpIMul %uint {pix} %kzsf_u10']]
        for c, v in enumerate(comps):
            e, p = new('se'), new('sp')
            out += ['               ' + q for q in [
                f'{e} = OpIAdd %uint {base} %kzsf_c{SLOT[var] + c}',
                f'{p} = OpAccessChain %kzsf_ptr_f %kzsf_lds {e}', f'OpStore {p} {v}']]
        stores += 1
        continue
    if in_main and s == f'{merge} = OpLabel':
        merge_label_seen = True
    if in_main and merge_label_seen and s == 'OpReturn':
        out += ['               ' + f for f in flush()]
    out.append(line)
if stores != 12:
    sys.exit(f'postpass_lds: expected 12 stores, rewrote {stores}')
print('\n'.join(out))
