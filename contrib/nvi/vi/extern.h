int cs_init __P((SCR *, VCS *));
int cs_next __P((SCR *, VCS *));
int cs_fspace __P((SCR *, VCS *));
int cs_fblank __P((SCR *, VCS *));
int cs_prev __P((SCR *, VCS *));
int cs_bblank __P((SCR *, VCS *));
int v_at __P((SCR *, VICMD *));
int v_chrepeat __P((SCR *, VICMD *));
int v_chrrepeat __P((SCR *, VICMD *));
int v_cht __P((SCR *, VICMD *));
int v_chf __P((SCR *, VICMD *));
int v_chT __P((SCR *, VICMD *));
int v_chF __P((SCR *, VICMD *));
int v_delete __P((SCR *, VICMD *));
int v_again __P((SCR *, VICMD *));
int v_exmode __P((SCR *, VICMD *));
int v_join __P((SCR *, VICMD *));
int v_shiftl __P((SCR *, VICMD *));
int v_shiftr __P((SCR *, VICMD *));
int v_suspend __P((SCR *, VICMD *));
int v_switch __P((SCR *, VICMD *));
int v_tagpush __P((SCR *, VICMD *));
int v_tagpop __P((SCR *, VICMD *));
int v_filter __P((SCR *, VICMD *));
int v_ex __P((SCR *, VICMD *));
int v_ecl_exec __P((SCR *));
int v_increment __P((SCR *, VICMD *));
int v_screen_copy __P((SCR *, SCR *));
int v_screen_end __P((SCR *));
int v_optchange __P((SCR *, int, char *, u_long *));
int v_iA __P((SCR *, VICMD *));
int v_ia __P((SCR *, VICMD *));
int v_iI __P((SCR *, VICMD *));
int v_ii __P((SCR *, VICMD *));
int v_iO __P((SCR *, VICMD *));
int v_io __P((SCR *, VICMD *));
int v_change __P((SCR *, VICMD *));
int v_Replace __P((SCR *, VICMD *));
int v_subst __P((SCR *, VICMD *));
int v_left __P((SCR *, VICMD *));
int v_cfirst __P((SCR *, VICMD *));
int v_first __P((SCR *, VICMD *));
int v_ncol __P((SCR *, VICMD *));
int v_zero __P((SCR *, VICMD *));
int v_mark __P((SCR *, VICMD *));
int v_bmark __P((SCR *, VICMD *));
int v_fmark __P((SCR *, VICMD *));
int v_emark __P((SCR *, VICMD *));
int v_match __P((SCR *, VICMD *));
int v_buildmcs __P((SCR *, char *));
int v_paragraphf __P((SCR *, VICMD *));
int v_paragraphb __P((SCR *, VICMD *));
int v_buildps __P((SCR *, char *, char *));
int v_Put __P((SCR *, VICMD *));
int v_put __P((SCR *, VICMD *));
int v_redraw __P((SCR *, VICMD *));
int v_replace __P((SCR *, VICMD *));
int v_right __P((SCR *, VICMD *));
int v_dollar __P((SCR *, VICMD *));
int v_screen __P((SCR *, VICMD *));
int v_lgoto __P((SCR *, VICMD *));
int v_home __P((SCR *, VICMD *));
int v_middle __P((SCR *, VICMD *));
int v_bottom __P((SCR *, VICMD *));
int v_up __P((SCR *, VICMD *));
int v_cr __P((SCR *, VICMD *));
int v_down __P((SCR *, VICMD *));
int v_hpageup __P((SCR *, VICMD *));
int v_hpagedown __P((SCR *, VICMD *));
int v_pagedown __P((SCR *, VICMD *));
int v_pageup __P((SCR *, VICMD *));
int v_lineup __P((SCR *, VICMD *));
int v_linedown __P((SCR *, VICMD *));
int v_searchb __P((SCR *, VICMD *));
int v_searchf __P((SCR *, VICMD *));
int v_searchN __P((SCR *, VICMD *));
int v_searchn __P((SCR *, VICMD *));
int v_searchw __P((SCR *, VICMD *));
int v_correct __P((SCR *, VICMD *, int));
int v_sectionf __P((SCR *, VICMD *));
int v_sectionb __P((SCR *, VICMD *));
int v_sentencef __P((SCR *, VICMD *));
int v_sentenceb __P((SCR *, VICMD *));
int v_status __P((SCR *, VICMD *));
int v_tcmd __P((SCR *, VICMD *, ARG_CHAR_T, u_int));
int v_txt __P((SCR *, VICMD *, MARK *,
   const CHAR_T *, size_t, ARG_CHAR_T, recno_t, u_long, u_int32_t));
int v_txt_auto __P((SCR *, recno_t, TEXT *, size_t, TEXT *));
int v_ulcase __P((SCR *, VICMD *));
int v_mulcase __P((SCR *, VICMD *));
int v_Undo __P((SCR *, VICMD *));
int v_undo __P((SCR *, VICMD *));
void v_eof __P((SCR *, MARK *));
void v_eol __P((SCR *, MARK *));
void v_nomove __P((SCR *));
void v_sof __P((SCR *, MARK *));
void v_sol __P((SCR *));
int v_isempty __P((CHAR_T *, size_t));
void v_emsg __P((SCR *, char *, vim_t));
int v_wordW __P((SCR *, VICMD *));
int v_wordw __P((SCR *, VICMD *));
int v_wordE __P((SCR *, VICMD *));
int v_worde __P((SCR *, VICMD *));
int v_wordB __P((SCR *, VICMD *));
int v_wordb __P((SCR *, VICMD *));
int v_xchar __P((SCR *, VICMD *));
int v_Xchar __P((SCR *, VICMD *));
int v_yank __P((SCR *, VICMD *));
int v_z __P((SCR *, VICMD *));
int vs_crel __P((SCR *, long));
int v_zexit __P((SCR *, VICMD *));
int vi __P((SCR **));
int v_curword __P((SCR *));
int vs_line __P((SCR *, SMAP *, size_t *, size_t *));
int vs_number __P((SCR *));
void vs_busy __P((SCR *, const char *, busy_t));
void vs_home __P((SCR *));
void vs_update __P((SCR *, const char *, const CHAR_T *));
void vs_msg __P((SCR *, mtype_t, char *, size_t));
int vs_ex_resolve __P((SCR *, int *));
int vs_resolve __P((SCR *, SCR *, int));
int vs_repaint __P((SCR *, EVENT *));
int vs_refresh __P((SCR *, int));
int vs_column __P((SCR *, size_t *));
size_t vs_screens __P((SCR *, recno_t, size_t *));
size_t vs_columns __P((SCR *, CHAR_T *, recno_t, size_t *, size_t *));
size_t vs_rcm __P((SCR *, recno_t, int));
size_t vs_colpos __P((SCR *, recno_t, size_t));
int vs_change __P((SCR *, recno_t, lnop_t));
int vs_sm_fill __P((SCR *, recno_t, pos_t));
int vs_sm_scroll __P((SCR *, MARK *, recno_t, scroll_t));
int vs_sm_1up __P((SCR *));
int vs_sm_1down __P((SCR *));
int vs_sm_next __P((SCR *, SMAP *, SMAP *));
int vs_sm_prev __P((SCR *, SMAP *, SMAP *));
int vs_sm_cursor __P((SCR *, SMAP **));
int vs_sm_position __P((SCR *, MARK *, u_long, pos_t));
recno_t vs_sm_nlines __P((SCR *, SMAP *, recno_t, size_t));
int vs_split __P((SCR *, SCR *, int));
int vs_vsplit __P((SCR *, SCR *));
int vs_discard __P((SCR *, SCR **));
int vs_fg __P((SCR *, SCR **, CHAR_T *, int));
int vs_bg __P((SCR *));
int vs_swap __P((SCR *, SCR **, char *));
int vs_resize __P((SCR *, long, adj_t));
