  s_add_u32 s16, s4, 0x10ff
  s_addc_u32 s17, s5, 0
  s_and_b32 s16, s16, 0xffffff00
  s_sub_u32 s18, s16, s4
  s_lshr_b32 s8, s16, 8
  s_lshl_b32 s19, s17, 24
  s_or_b32 s8, s8, s19
  s_lshr_b32 s19, s17, 8
  s_and_b32 s19, s19, 0xff
  s_or_b32 s9, s19, @WORD1@
  s_mov_b32 s10, @WORD2@
  s_mov_b32 s11, @WORD3@
  s_mov_b32 s12, @WORD4@
  s_mov_b32 s13, @WORD5@
  s_mov_b32 s14, @WORD6@
  s_mov_b32 s15, @WORD7@
  v_mov_b32 v34, v4
  v_mov_b32 v35, v5
  v_mov_b32 v30, v6
  v_xor_b32 v31, 1, v6
  v_xor_b32 v32, 2, v6
  v_xor_b32 v33, 3, v6
  v_mov_b32 v36, 0
@ACCESS@
  s_waitcnt vmcnt(0)
  s_waitcnt_vscnt null, 0x0
  buffer_gl0_inv
  buffer_gl1_inv
  image_load v[40:43], v[34:35], s[8:15] dmask:0xf dim:SQ_RSRC_IMG_2D unorm glc
  s_waitcnt vmcnt(0)
  v_mov_b32 v50, 0x7f0
  v_mul_lo_u32 v50, v50, v0
  v_add_nc_u32 v50, 0x200, v50
  v_mov_b32 v51, 0
  v_mov_b32 v52, -1
  v_mov_b32 v53, 0
  v_mov_b32 v57, -1
  v_mov_b32 v58, 0
  s_movk_i32 s20, 0x1fc
.Ltype0_scan:
  global_load_dword v54, v50, s[4:5] glc dlc
  v_lshrrev_b32 v55, 2, v50
  v_xor_b32 v55, 0xa5a50000, v55
  s_waitcnt vmcnt(0)
  v_cmp_ne_u32 vcc_lo, v54, v55
  v_cndmask_b32 v56, 0, 1, vcc_lo
  v_add_nc_u32 v51, v51, v56
  v_cmp_eq_u32 s21, -1, v52
  s_and_b32 s21, s21, vcc_lo
  v_cndmask_b32 v52, v52, v50, s21
  v_cndmask_b32 v53, v53, v54, s21
  v_cndmask_b32 v57, v57, v50, vcc_lo
  v_cndmask_b32 v58, v58, v54, vcc_lo
  v_add_nc_u32 v50, 4, v50
  s_sub_u32 s20, s20, 1
  s_cmp_lg_u32 s20, 0
  s_cbranch_scc1 .Ltype0_scan
  v_mov_b32 v10, v51
  v_subrev_nc_u32 v11, s18, v52
  v_mov_b32 v12, v53
  v_subrev_nc_u32 v13, s18, v57
  v_mov_b32 v14, v58
  v_mov_b32 v15, v40
  v_mov_b32 v16, v41
  v_mov_b32 v17, v42
  v_mov_b32 v18, v43
  v_mov_b32 v19, v36
  v_mov_b32 v20, s18
  v_mov_b32 v21, 0x7e57ab1e
