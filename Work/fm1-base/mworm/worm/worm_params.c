#include <stdio.h>
#include <string.h>
#include <math.h>
#include "worm.h"
#include "worm_fm_harm.h"   /* const int16_t worm_fm_harm[FM_NH][128] во flash */
const char *const L_ON[]={"OFF","ON"},*const L_LEN[]={"0.063","0.125","0.25","0.375","0.5","0.625","0.75","0.875","1/16","1/8","3","1/4","5","6","7","1/2","9","10","11","12","13","14","15","16","17","18","19","20","21","22","23","24","25","26","27","28","29","30","31","32","33","34","35","36","37","38","39","40","41","42","43","44","45","46","47","48","49","50","51","52","53","54","55","56","57","58","59","60","61","62","63","64","65","66","67","68","69","70","71","72","73","74","75","76","77","78","79","80","81","82","83","84","85","86","87","88","89","90","91","92","93","94","95","96","97","98","99","100","101","102","103","104","105","106","107","108","109","110","111","112","113","114","115","116","117","118","119","120","121","122","123","124","125","126","127","128"},
*const L_COND[]={"--","1:2","2:2","1:3","2:3","3:3","1:4","2:4","3:4","4:4","1:8","8:8","1ST","FILL","4BAR","NEV"},*const L_ALGO[]={"VA","FM","WT","AT","GS","EL","VA","VA","VA"},
*const L_DIST[]={"OFF","OVR","DIST","FUZZ"},*const L_XT[]={"1/16","1/8","3/16","1/4","1/2"},*const L_FT[]={"OFF","LP","HP","BP"},
*const L_LD[]={"OFF","PITCH","OCT","DETUNE","SUB","LEVEL","PW","CUT1","RES1","CUT2","RES2","PAN","VOL","DRIVE","FBMIX"},*const L_LW[]={"TRI","SIN","SQR","SAW","RND","EXP","RAMP","TDS"},*const L_LM[]={"FREE","HOLD","ONE","HALF"},*const L_LY[]={"OFF","ON"},*const L_LDIV[]={"1/16","1/8","1/4","1/2","1/1","2/1","4/1"},*const L_SCALE[]={"CHR","MAJ","MIN","DOR","PHR","LYD","MIX","PEN"},
*const L_NOTE[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"},*const L_CHORD[]={"OFF","OCT","5TH","MAJ","MIN"},*const L_PRE[]={"OFF","1BAR"},
*const L_SND[]={"CLK"},*const L_DIR[]={">>","<<","<>","??"},*const L_SPD[]={"1/2X","1X","2X"},
*const L_ARP[]={"OFF","UP","DOWN","UPDN","RND"},
*const L_MLW[]={"TRI","SIN","SQR","SAW","RND","EXP","RAMP"},*const L_AMO[]={"OFF","M1","M2","M3","M4"},
*const L_RTR[]={"OFF","1/2","1/4","1/8","1/12","1/16","1/24","1/32","1/48","1/64"},*const L_RTL[]={"LEN","1","2","4","8","16"};
/* RETRIG: доля целой ноты (шаг 1X = 1/16): интервал = 16 шагов / den */
const uint8_t worm_rtr_den[10]={0,2,4,8,12,16,24,32,48,64};
/* WT / HIVE / CHIP / TOY удалены (нагрузка CPU и 32 КБ таблиц в пуле): номера оставлены пустыми ради совместимости сохранённых банков */
const char *const worm_algo_names[9]={"VIRTUAL ANALOG","FM","WAVETABLE","-","-","-","-","-","-"};
const int8_t worm_algo_list[NALGO_ON]={ALGO_VA,ALGO_FM,ALGO_WT};
int worm_algo_ok(int a){return a==ALGO_VA||a==ALGO_FM||a==ALGO_WT;}
int worm_algo_fix(int a){return worm_algo_ok(a)?a:ALGO_VA;}
/* Полный список DEST-целей LFO. id=-2 = заголовок категории, -1 = OFF, иначе параметр PLIST.
 * OSC-группа зависит от алгоритма (VA/FM/WT) — в списке приведены WT; UI подменяет имена по P_OA. */
const worm_dest_t worm_dest_list[] = {
 {-1,"OFF"},
 {-2,"OSC1"},
 {P_OW,"OSC1 WAVE"}, {P_OP,"OSC1 POS"}, {P_OT,"OSC1 TUNE"}, {P_OO,"OSC1 OCT"}, {P_OPI,"OSC1 PITCH"},
 {P_OD,"OSC1 DETUNE"}, {P_OS,"OSC1 SUB"}, {P_OL,"OSC1 LEVEL"},
 {-2,"OSC2"},
 {P_OW2,"OSC2 WAVE"}, {P_OP2,"OSC2 POS"}, {P_OT2,"OSC2 TUNE"}, {P_OO2,"OSC2 OCT"}, {P_OPI2,"OSC2 PITCH"},
 {P_OD2,"OSC2 DETUNE"}, {P_OS2,"OSC2 SUB"}, {P_OL2,"OSC2 LEVEL"},
 {-2,"AMP"},
 {P_ADW,"AMP D/WET"}, {P_ADS,"AMP DIST"}, {P_APAN,"AMP PAN"}, {P_AVOL,"AMP VOL"}, {P_ADR,"AMP DRIVE"}, {P_TRN,"AMP TRANSIENT"},
 {-2,"FILTER1"},
 {P_F1C,"FLT1 CUT"}, {P_F1R,"FLT1 RESO"}, {P_F1T,"FLT1 TYPE"}, {P_F1E,"FLT1 ENV"}, {P_F1W,"FLT1 D/WET"},
 {-2,"FILTER2"},
 {P_F2C,"FLT2 CUT"}, {P_F2R,"FLT2 RESO"}, {P_F2T,"FLT2 TYPE"}, {P_F2E,"FLT2 ENV"}, {P_F2W,"FLT2 D/WET"},
 {-2,"SEND"},
 {P_SDL,"SEND DELAY"}, {P_SRV,"SEND REVERB"}, {P_SCH,"SEND CHORUS"},
 {-2,"DELAY"},
 {P_XT,"DELAY TIME"}, {P_XFB,"DELAY FDBK"}, {P_XFL,"DELAY FILT"}, {P_XMX,"DELAY MIX"},
 {-2,"REVERB"},
 {P_RSZ,"REV SIZE"}, {P_RDC,"REV DECAY"}, {P_RDM,"REV DAMP"}, {P_RMX,"REV MIX"},
 {-2,"CHORUS"},
 {P_CRT,"CHO RATE"}, {P_CDP,"CHO DEPTH"}, {P_CDL,"CHO DELAY"}, {P_CMX,"CHO MIX"},
 {-2,"OSC1 ENV"},
 {P_E0A,"O1ENV ATK"}, {P_E0D,"O1ENV DEC"}, {P_E0S,"O1ENV SUS"}, {P_E0R,"O1ENV REL"}, {P_E0V,"O1ENV VOL"},
 {-2,"OSC2 ENV"},
 {P_E3A,"O2ENV ATK"}, {P_E3D,"O2ENV DEC"}, {P_E3S,"O2ENV SUS"}, {P_E3R,"O2ENV REL"}, {P_E3V,"O2ENV VOL"},
 {-2,"FLT1 ENV"},
 {P_E1A,"F1ENV ATK"}, {P_E1D,"F1ENV DEC"}, {P_E1S,"F1ENV SUS"}, {P_E1R,"F1ENV REL"},
 {-2,"FLT2 ENV"},
 {P_E2A,"F2ENV ATK"}, {P_E2D,"F2ENV DEC"}, {P_E2S,"F2ENV SUS"}, {P_E2R,"F2ENV REL"},
 {-2,"LFO2"},
 {P_L1S,"LFO2 SPD"}, {P_L1P,"LFO2 DPT"}, {P_L1W,"LFO2 WAVE"}, {P_L1Y,"LFO2 SYNC"},
 {-2,"LFO3"},
 {P_L2S,"LFO3 SPD"}, {P_L2P,"LFO3 DPT"}, {P_L2W,"LFO3 WAVE"}, {P_L2Y,"LFO3 SYNC"},
 {-2,"LFO4"},
 {P_L3S,"LFO4 SPD"}, {P_L3P,"LFO4 DPT"}, {P_L3W,"LFO4 WAVE"}, {P_L3Y,"LFO4 SYNC"},
 {-2,"OSC1 FM"},
 {P_FR1,"FM1 RATIO"}, {P_FF1,"FM1 FDBK"}, {P_FE1,"FM1 I.ENV"}, {P_FV1,"FM1 I.VEL"},
 {-2,"OSC2 FM"},
 {P_FR2,"FM2 RATIO"}, {P_FF2,"FM2 FDBK"}, {P_FE2,"FM2 I.ENV"}, {P_FV2,"FM2 I.VEL"},
 {-2,"FREE ENV"},
 {P_E4A,"FREE ATK"}, {P_E4D,"FREE DEC"}, {P_E4S,"FREE SUS"}, {P_E4R,"FREE REL"}, {P_E4P,"FREE DEPTH"},
 /* в самом конце — индексы сохранённых проектов не сдвигаются. ALL OSC PITCH = PITCH обоих OSC (синт добавляет вторую цель, см. worm_dest_all).
  * UI показывает ALL OSC PITCH сразу после OFF и подмешивает FM-параметры в группы OSC1/OSC2, не меняя эти индексы (dest_order_init в worm_ui.c). */
 {-2,"MULTI"},
 {P_OPI,"ALL OSC PITCH"},
 /* --- дописано для FM (индексы 112..122; старые не сдвигались) --- */
 {P_WH1,"SYN B2 OFS"}, {P_WF1,"SYN A DELAY"}, {P_WM1,"SYN B DELAY"},
 {P_VPAL,"SYN PITCH ALL"}, {P_VPAB,"SYN PITCH A+B2"},
 {P_FR1,"SYN RATIO ALL"}, {P_OP2,"SYN AB LEVEL"}, {P_OT,"SYN AB ATK"}, {P_OS,"SYN AB DEC"}, {P_OL,"SYN AB END"}, {P_WF1,"SYN AB DELAY"},
};
const int worm_dest_n = (int)(sizeof worm_dest_list / sizeof worm_dest_list[0]);
int worm_dest_param(int dest_idx){return dest_idx>=0&&dest_idx<worm_dest_n?worm_dest_list[dest_idx].id:-1;}
int worm_dest_index(int param_id){for(int i=0;i<worm_dest_n;i++)if(worm_dest_list[i].id==param_id)return i;return -1;}
/* Составные DEST: один LFO двигает несколько параметров сразу (главный id — в списке, остальные — здесь). */
static const struct { uint8_t i; int16_t a, b; } DEXT[] = {
 {111, P_OPI2, -1},                 /* ALL OSC PITCH (VA/WT): PITCH обоих OSC */
 {117, P_FR2, P_FF1},               /* RATIO ALL: RATIO C + A + B */
 {118, P_OP, -1},                   /* AB LEVEL: LEV A + LEV B */
 {119, P_OT2, -1},                  /* AB ATTACK */
 {120, P_OD2, -1},                  /* AB DECAY */
 {121, P_OS2, -1},                  /* AB END */
 {122, P_WM1, -1},                  /* AB DELAY: DEL A + DEL B */
};
int worm_dest_ext(int dest_idx,int16_t *ex){
 for(unsigned q=0;q<sizeof DEXT/sizeof DEXT[0];q++)if(DEXT[q].i==dest_idx){int n=1;if(ex){ex[0]=DEXT[q].a;ex[1]=DEXT[q].b;}if(DEXT[q].b>=0)n=2;return n;}
 return 0;}
void worm_str5(char *d,const char *s){int i=0;if(!s)s="";for(;i<5&&s[i];i++)d[i]=s[i];d[i]=0;}   /* %.5s не работает в прошивочной libc */

/* ================= FM: слоты, таблицы, подписи ================= */
/* Топология алгоритмов по Appendix A.3 мануала Digitone. Операторы C,A,B1,B2 = биты 1,2,4,8.
 * mod[op] — кто модулирует оператор; xd/xs, yd/ys — несущие на выходах X и Y: d = «пунктир» (громкость не зависит от огибающей оператора),
 * s = «сплошная» (умножается на огибающую/уровень оператора); fb — оператор с FDBK; ord — порядок счёта (модуляторы раньше целей). */
const fmalg_t worm_fm_algo[8] = {
 /*1*/ {{6, 0, 8, 0}, 1, 0, 4, 0, 1, {3, 1, 2, 0}},   /* A>C, B2>B1>C;        X=C            Y=B1; FDBK A */
 /*2*/ {{2, 0, 8, 0}, 1, 0, 4, 0, 3, {3, 1, 2, 0}},   /* A>C, B2>B1;          X=C            Y=B1; FDBK B2 */
 /*3*/ {{2, 0, 2, 2}, 9, 0, 4, 0, 1, {1, 0, 2, 3}},   /* A>(C,B1,B2);         X=C+B2         Y=B1; FDBK A */
 /*4*/ {{2, 4, 8, 0}, 1, 0, 4, 0, 3, {3, 2, 1, 0}},   /* B2>B1>A>C;           X=C            Y=B1; FDBK B2 */
 /*5*/ {{2,12, 0, 0}, 1, 0, 2, 0, 2, {3, 2, 1, 0}},   /* (B1+B2)>A>C;         X=C            Y=A;  FDBK B1 */
 /*6*/ {{10, 0,10, 0}, 1, 0, 4, 0, 1, {1, 3, 0, 2}},  /* (A+B2)>C, (A+B2)>B1; X=C            Y=B1; FDBK A */
 /*7*/ {{2, 0, 8, 0}, 1, 2, 4, 8, 1, {3, 2, 1, 0}},   /* A>C, B2>B1;          X=C+A(сплош.)  Y=B1+B2(сплош.); FDBK A */
 /*8*/ {{2, 0, 0, 0}, 1, 8, 0, 4, 2, {1, 0, 2, 3}} }; /* A>C; B2, B1 — несущие; X=C+B2(сплош.) Y=B1(сплош.); FDBK B1 */

/* k (FM_*) -> id слота PLIST. Занято ровно 27 слотов OSC1/OSC2 (+WH1/WF1/WM1); ID и порядок PLIST не менялись -> банки/p-lock совместимы. */
const uint8_t worm_fm_slot[FM_N] = {
 P_OW, P_FR1, P_FR2, P_FF1, P_FF2, P_OD, P_FE1, P_OL2,      /* ALGO, RATIO C/A/B, HARM, DTUN, FDBK, MIX */
 P_FE2, P_FV1, P_FV2, P_WH1,                                /* смещения ratio C / A / B1 / B2 */
 P_OT, P_OS, P_OL, P_OP2, P_OT2, P_OD2, P_OS2, P_OP,        /* ATK/DEC/END/LEV A, ATK/DEC/END/LEV B */
 P_WF1, P_OW2, P_OO, P_OPI, P_WM1, P_OO2, P_OPI2 };         /* A DELAY, A TRIG, A RESET, PHASE RESET, B DELAY, B TRIG, B RESET */
const float worm_fm_rc[FM_NRC]={.25f,.5f,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};   /* C: почти целые */
const float worm_fm_ra[FM_NRA]={.25f,.5f,.75f,1,1.25f,1.5f,1.75f,2,2.5f,3,3.5f,4,4.5f,5,5.5f,6,6.5f,7,7.5f,8,9,10,11,12,13,14,15,16};   /* A: богаче */
const float worm_fm_rb[FM_NRB]={.25f,.5f,1,2,3,4,5,6,8,12,16};   /* B1/B2: пара по кругу, B2 — младшая цифра (индекс = i1*11+i2) */
enum { K_ALGO,K_RC,K_RA,K_RB,K_HARM,K_NUM,K_MIX,K_OFS,K_ONOFF,K_PHRT };
typedef struct { const char *lb, *it; int16_t mn, mx, df; uint8_t kind; } fmd_t;
static const fmd_t FMD[FM_N] = {
 {"ALGO","Algorithm",0,7,0,K_ALGO},{"RAT C","Ratio C",0,FM_NRC-1,2,K_RC},{"RAT A","Ratio A",0,FM_NRA-1,3,K_RA},{"RAT B","Ratio B",0,FM_NRB*FM_NRB-1,24,K_RB},
 {"HARM","Harmonics",-52,52,0,K_HARM},{"DTUN","Detune",0,127,0,K_NUM},{"FDBK","Feedback",0,127,0,K_NUM},{"MIX","Mix",0,127,64,K_MIX},
 {"OFS C","Ratio C Offset",0,127,64,K_OFS},{"OFS A","Ratio A Offset",0,127,64,K_OFS},{"OF B1","Ratio B1 Offset",0,127,64,K_OFS},{"OF B2","Ratio B2 Offset",0,127,64,K_OFS},
 {"ATK A","A Attack",0,127,0,K_NUM},{"DEC A","A Decay",0,127,50,K_NUM},{"END A","A End",0,127,0,K_NUM},{"LEV A","A Level",0,127,50,K_NUM},
 {"ATK B","B Attack",0,127,0,K_NUM},{"DEC B","B Decay",0,127,50,K_NUM},{"END B","B End",0,127,0,K_NUM},{"LEV B","B Level",0,127,50,K_NUM},
 {"DEL A","A Delay",0,127,0,K_NUM},{"TRG A","A Trig",0,1,1,K_ONOFF},{"RST A","A Reset",0,1,1,K_ONOFF},{"PHRT","Phase Reset",0,4,1,K_PHRT},
 {"DEL B","B Delay",0,127,0,K_NUM},{"TRG B","B Trig",0,1,1,K_ONOFF},{"RST B","B Reset",0,1,1,K_ONOFF} };
int worm_fm_of(int id){for(int k=0;k<FM_N;k++)if(worm_fm_slot[k]==id)return k;return -1;}
void worm_fm_range(int k,int *mn,int *mx){*mn=FMD[k].mn;*mx=FMD[k].mx;}
int worm_fm_clamp(int k,int v){if(v<FMD[k].mn)v=FMD[k].mn;if(v>FMD[k].mx)v=FMD[k].mx;return v;}
const char *worm_fm_label(int k){return FMD[k].lb;}
void worm_fm_defaults(int16_t *p){for(int k=0;k<FM_N;k++)p[worm_fm_slot[k]]=FMD[k].df;}
void worm_fm_restore(int16_t *p){for(int k=0;k<FM_N;k++)p[worm_fm_slot[k]]=worm_pinfo[worm_fm_slot[k]].def;}
static void rfmt(char *o,float r){int m=(int)(r*100.f+.5f),w=m/100,f=m%100;   /* 0.25 -> ".25", 1.5 -> "1.5", 16 -> "16" */
 if(!f)sprintf(o,"%d",w);else if(w)sprintf(o,f%10?"%d.%02d":"%d.%d",w,f%10?f:f/10);else sprintf(o,f%10?".%02d":".%d",f%10?f:f/10);}
void worm_fm_fmt(int k,int v,char *o){const fmd_t *d=&FMD[k];char a[10],b[10];if(v<d->mn)v=d->mn;if(v>d->mx)v=d->mx;
 switch(d->kind){
 case K_ALGO:sprintf(o,"%d",v+1);break;
 case K_RC:rfmt(o,worm_fm_rc[v]);break;
 case K_RA:rfmt(o,worm_fm_ra[v]);break;
 case K_RB:rfmt(a,worm_fm_rb[v/FM_NRB]);rfmt(b,worm_fm_rb[v%FM_NRB]);sprintf(o,"%s:%s",a,b);break;
 case K_HARM:{int x=v<0?-v:v;if(!v)strcpy(o,"0");else sprintf(o,"%c%d.%d",v<0?'-':'+',x/2,(x&1)*5);break;}
 case K_MIX:sprintf(o,"%+d",v-64);break;
 case K_OFS:{int x=v-64,u=x<0?-x:x,m=(u*625+50)/100;if(!x)strcpy(o,"0");else sprintf(o,"%c%d.%02d",x<0?'-':'+',m/100,m%100);break;}   /* шаг 1/16 */
 case K_ONOFF:strcpy(o,v?"ON":"OFF");break;
 case K_PHRT:{static const char *const N[5]={"OFF","ALL","C","AB","A+B2"};strcpy(o,N[v]);break;}
 default:sprintf(o,"%d",v);}}

/* ================= DEST: группы, порядок, имена ================= */
/* Индекс DEST = то, что хранится в банках (L0D..), поэтому старые индексы НЕ менялись, новые дописаны в конец списка.
 * Порядок показа в выборе задан ниже и зависит от движка: у FM — группа SYN в порядке Appendix C мануала Digitone. */
enum { DG_NONE,DG_SYN,DG_OSC1,DG_OSC2,DG_AMP,DG_FLT1,DG_FLT2,DG_SEND,DG_DLY,DG_REV,DG_CHO,DG_AEN1,DG_AEN2,DG_FEN1,DG_FEN2,DG_LFO2,DG_LFO3,DG_LFO4,DG_FREE,DG_N };
static const char *const DGN[DG_N]={"","OSC","OSC1","OSC2","AMP","FLT1","FLT2","SEND","DELAY","REVERB","CHORUS","AENV1","AENV2","FENV1","FENV2","LFO2","LFO3","LFO4","FREE"};
const char *worm_dest_grp(int g){return g>=0&&g<DG_N?DGN[g]:"";}
static const uint8_t FMORD[]={2,95,100,96,101,7,97,18, 4,8,9,12, 13,16,17,3, 113,114, 115,116,117,118,119,120,121,122, 102,98,103,112};   /* Appendix C: Algorithm .. B2 Offset */
static const struct { uint8_t g, i0, n, wt_hide; } CG[]={
 {DG_AMP,20,6,0},{DG_FLT1,27,5,0},{DG_FLT2,33,5,1},{DG_SEND,39,3,0},{DG_DLY,43,4,0},{DG_REV,48,4,0},{DG_CHO,53,4,0},
 {DG_AEN1,58,5,0},{DG_AEN2,64,5,1},{DG_FEN1,70,4,0},{DG_FEN2,75,4,1},{DG_LFO2,80,4,0},{DG_LFO3,85,4,0},{DG_LFO4,90,4,0},{DG_FREE,105,5,0}};
int worm_dest_order(int algo,uint8_t *idx,uint8_t *grp){int n=0;
#define PUSH(i,g) do{idx[n]=(uint8_t)(i);grp[n]=(uint8_t)(g);n++;}while(0)
 PUSH(0,DG_NONE);
 if(algo==ALGO_FM){for(unsigned q=0;q<sizeof FMORD;q++)PUSH(FMORD[q],DG_SYN);}
 else{for(int i=2;i<=9;i++)PUSH(i,DG_OSC1);
  if(algo!=ALGO_WT){for(int i=11;i<=18;i++)PUSH(i,DG_OSC2);PUSH(111,DG_OSC2);}}
 for(unsigned q=0;q<sizeof CG/sizeof CG[0];q++){if(algo==ALGO_WT&&CG[q].wt_hide)continue;for(int i=0;i<CG[q].n;i++)PUSH(CG[q].i0+i,CG[q].g);}
#undef PUSH
 return n;}
/* Имя DEST для строки списка: Title Case; аббревиатуры раскрыты. Группа показана отдельной колонкой, поэтому без префикса. */
static void tcase(char *o,const char *s){
 static const char *const AB[][2]={{"CUT","Cutoff"},{"RESO","Resonance"},{"ATK","Attack"},{"DEC","Decay"},{"SUS","Sustain"},{"REL","Release"},{"SPD","Speed"},{"DPT","Depth"},{"VOL","Volume"},{"FDBK","Feedback"},{"FILT","Filter"},{"D/WET","Dry/Wet"},{"ENV","Env amount"},{"POS","Position"},{"DIST","Distortion"},{"CHO","Chorus"},{"REV","Reverb"},{"DLY","Delay"}};
 char t[16];int n=0;while(*s&&*s!=' '&&n<15)t[n++]=*s++;t[n]=0;
 for(unsigned q=0;q<sizeof AB/sizeof AB[0];q++)if(!strcmp(t,AB[q][0])){strcpy(o,AB[q][1]);return;}
 if(!strcmp(t,"PW")){strcpy(o,"PW");return;}
 for(int i=0;t[i];i++)o[i]=(char)(i&&t[i]>='A'&&t[i]<='Z'?t[i]+32:t[i]);o[n]=0;}
const char *worm_dest_item(int dest_idx,int algo){static char buf[24];
 int id=worm_dest_param(dest_idx);
 if(id==-1)return "Off";
 if(dest_idx==111)return "Pitch all";
 if(algo==ALGO_FM&&dest_idx>=115){static const char *const N[]={"Pitch All","Pitch A and B2","Ratio All","AB Level","AB Attack","AB Decay","AB End","AB Delay"};if(dest_idx<=122)return N[dest_idx-115];}
 if(algo==ALGO_FM){int k=worm_fm_of(id);if(k>=0)return FMD[k].it;}
 if(id==P_OW)return algo==ALGO_WT?"Table":"Wave";
 if(id==P_OW2)return "Wave";
 if(id==P_OP||id==P_OP2)return algo==ALGO_WT?"Position":"PW";
 if(id==P_OL||id==P_OL2)return "Level";
 if(id==P_OT||id==P_OT2)return "Tune";
 if(id==P_OO||id==P_OO2)return "Octave";
 if(id==P_OPI||id==P_OPI2)return "Pitch";
 if(id==P_OD||id==P_OD2)return "Detune";
 if(id==P_OS||id==P_OS2)return "Sub";
 {const char *nm=worm_dest_list[dest_idx].name,*sp=nm;while(*sp&&*sp!=' ')sp++;tcase(buf,*sp?sp+1:nm);return buf;}}
/* полное имя (MAP, макросы LIVE): у FM — "SYN ..." прописными */
const char *worm_dest_name(int dest_idx,int algo){static char buf[24];
 int id=worm_dest_param(dest_idx);
 if(id==-2||id==-1||dest_idx<0||dest_idx>=worm_dest_n)return worm_dest_list[dest_idx<0?0:dest_idx>=worm_dest_n?0:dest_idx].name;
 if(algo==ALGO_FM&&dest_idx<115){int k=worm_fm_of(id);if(k>=0){const char *s=FMD[k].it;int n=4;strcpy(buf,"SYN ");for(;*s&&n<22;s++)buf[n++]=(char)(*s>='a'&&*s<='z'?*s-32:*s);buf[n]=0;return buf;}}
 if(algo!=ALGO_FM&&dest_idx>=112&&dest_idx<=122)return worm_dest_list[dest_idx].name;
 if(algo==ALGO_WT&&(id==P_OW||id==P_OP))return id==P_OW?"OSC1 TABLE":"OSC1 POS";
 if(id==P_OP&&algo==ALGO_VA)return "OSC1 PW";if(id==P_OP2&&algo==ALGO_VA)return "OSC2 PW";
 return worm_dest_list[dest_idx].name;}

/* Короткие имена DEST (<=5 символов — влезают под ручку шириной 40 px). OSC1/2 -> O1/O2, FLT -> F1/F2, ENV -> E1.., LFO -> L2.. */
const char *worm_dest_short(int dest_idx,int algo){static char b[8];
 int16_t ex[2];int id=worm_dest_param(dest_idx);
 if(id==-1)return "OFF";
 if(dest_idx==111)return "ALLP";
 if(dest_idx>=112&&dest_idx<=122){static const char *const N[]={"OF B2","DEL A","DEL B","PITCH","PIT.B","RATAL","ABLEV","ABATK","ABDEC","ABEND","ABDLY"};return N[dest_idx-112];}
 if(id==-2){worm_str5(b,worm_dest_list[dest_idx].name);return b;}
 if(id<0)return "?";(void)ex;
 if(algo==ALGO_FM){int k=worm_fm_of(id);if(k>=0)return FMD[k].lb;}
 if(algo<0||algo>2)algo=0;
 if((id>=P_OW&&id<=P_OL)||(id>=P_OW2&&id<=P_OL2)){int o2=id>=P_OW2,k=id-(o2?P_OW2:P_OW);
  static const char *const W_[3]={"WAV","ALG","TBL"},*const P_[3]={"PW","IDX","POS"};
  const char *s=k==0?W_[algo]:k==1?P_[algo]:k==2?"TUN":k==3?"OCT":k==4?"DET":k==5?"SUB":"LVL";
  snprintf(b,sizeof b,"O%d%s",o2?2:1,s);return b;}
 if(id==P_OPI)return "O1PIT";if(id==P_OPI2)return "O2PIT";if(id==P_TRN)return "TRANS";
 if(id>=P_ADW&&id<=P_ADR){static const char *const N[5]={"DWET","DIST","PAN","VOL","DRIVE"};return N[id-P_ADW];}
 if(id>=P_F1C&&id<=P_F2E){int k=id-P_F1C;static const char *const N[4]={"CUT","RES","TYP","ENV"};snprintf(b,sizeof b,"F%d%s",k>=4?2:1,N[k&3]);return b;}
 if(id==P_F1W||id==P_F2W){snprintf(b,sizeof b,"F%dDW",id==P_F2W?2:1);return b;}
 if(id>=P_SDL&&id<=P_SCH){static const char *const N[3]={"S.DLY","S.REV","S.CHO"};return N[id-P_SDL];}
 if(id>=P_XT&&id<=P_XMX){static const char *const N[4]={"D.TIM","D.FB","D.FLT","D.MIX"};return N[id-P_XT];}
 if(id>=P_RSZ&&id<=P_RMX){static const char *const N[4]={"R.SIZ","R.DEC","R.DMP","R.MIX"};return N[id-P_RSZ];}
 if(id>=P_CRT&&id<=P_CMX){static const char *const N[4]={"C.RAT","C.DEP","C.DLY","C.MIX"};return N[id-P_CRT];}
 if(id>=P_E0A&&id<=P_E3R){int k=id-P_E0A;static const char *const N[4]={"ATK","DEC","SUS","REL"},*const G[4]={"A1","F1","F2","A2"};snprintf(b,sizeof b,"%s%s",G[k/4],N[k&3]);return b;}   /* A1/A2 = amp ENV осцилляторов 1/2, F1/F2 = ENV фильтров */
 if(id==P_E0V||id==P_E3V){snprintf(b,sizeof b,"%s%s",id==P_E3V?"A2":"A1","VOL");return b;}
 if(id>=P_E4A&&id<=P_E4P){static const char *const N[6]={"ATK","DEC","SUS","REL","DEST","DPT"};return N[id-P_E4A];}
 if(id>=P_L0D&&id<P_L0D+NLFO*9){int l=(id-P_L0D)/9,r=(id-P_L0D)%9;const char *s=r==1?"SPD":r==2?"DPT":r==3?"WAV":r==5?"SYN":"?";snprintf(b,sizeof b,"L%d%s",l+1,s);return b;}
 worm_str5(b,worm_pinfo[id].name);return b;}
const pinfo_t worm_pinfo[NPALL]={
#define X(id,n,a,b,d,f,l) {n,a,b,d,f,l},
PLIST(X)
MLIST(X)
RLIST(X)
#undef X
};
/* MIDI-дорожка: свои параметры (id >= NP, кроме P_AMO — он общий) + NOTE (ROOT/VEL/LEN/PROB/COND) и EUCLID звуковых id */
int worm_mpos(int id){
    if(id==P_AMO||id>P_AMO)return -1;   /* после P_AMO — ретриггер шага: у дорожки своего значения нет */
    if(id>=NP)return id-NP;
    if(id>=P_NR&&id<=P_NCD)return (P_AMO-NP)+(id-P_NR);
    if(id>=P_EUH&&id<=P_EUM)return (P_AMO-NP)+5+(id-P_EUH);
    return -1;}
const uint8_t worm_scale_iv[8][12]={{0,1,2,3,4,5,6,7,8,9,10,11},{0,2,4,5,7,9,11},{0,2,3,5,7,8,10},{0,2,3,5,7,9,10},{0,1,3,5,7,8,10},{0,2,4,6,7,9,11},{0,2,4,5,7,9,10},{0,2,4,7,9}};
const uint8_t worm_scale_n[8]={12,7,7,7,7,7,7,5};
typedef struct { const char *n; int id[8]; int16_t v[8]; } preset_t;
static const preset_t PRS[NPRESET]={
{"INIT",{-1}},{"SOFT KEYS",{P_OW,P_E0A,P_E0R,P_F1C,-1},{1,10,50,80}},{"FM BELL",{P_OA,P_OW,P_FR2,P_FE1,P_E0A,P_E0D,P_E0S,P_E0R},{1,4,10,12,0,70,0,70}},
{"WARM PAD",{P_OD,P_E0A,P_E0R,P_F1C,-1},{40,60,80,70}},{"ACID BASS",{P_F1C,P_F1R,P_F1E,P_E1D,P_E1S,P_E0S,P_E0R,-1},{50,90,120,35,0,100,15}},
{"PLUCK",{P_OW,P_E0D,P_E0S,P_E0R,P_F1E,P_E1D,P_E1S,-1},{3,30,0,25,110,25,0}},{"BRASS",{P_E0A,P_F1C,P_F1E,P_E1A,P_E1S,-1},{25,60,100,30,90}},
{"WOBBLE",{P_OW,P_L0D,P_L0S,P_L0P,P_F1R,P_F1C,P_F1T,-1},{3,P_F1C,35,110,70,55,1}},{"GLASS",{P_OA,P_OW,P_FR2,P_FF2,P_E0A,P_E0R,-1},{1,2,11,12,8,90}},
{"NOISE HAT",{P_OA,P_OW,P_E0D,P_E0S,P_E0R,P_F1T,P_F1C,-1},{0,5,15,0,10,2,100}},{"SUB",{P_OW,P_OS,P_OO,P_E0S,-1},{0,110,-1,110}},
{"STAB",{P_OA,P_OW,P_E0D,P_E0S,P_E0R,-1},{0,2,25,20,20}},{"DRONE",{P_OD,P_OS,P_E0A,P_E0R,P_L0D,P_L0S,P_L0P,-1},{60,80,90,100,P_OD,3,40}},
{"ARP SAW",{P_E0D,P_E0S,P_E0R,P_XMX,P_SDL,-1},{20,30,15,50,40}},
{"SPARK",{P_OW,P_E0A,P_E0D,P_E0S,P_E0R,P_F1C,-1},{2,2,25,0,30,90}},
{"HOUSE",{P_OW,P_E0A,P_E0R,P_F1C,P_F1E,-1},{0,8,45,75,60}},
{"TECHNO",{P_OW,P_E0D,P_E0S,P_E0R,P_F1R,P_F1C,-1},{3,20,0,20,80,65}},
{"DUB",{P_OW,P_OS,P_E0A,P_E0R,P_F1C,P_F1R,-1},{0,90,15,110,55,40}},
{"AMBIENT",{P_E0A,P_E0R,P_SDL,P_SRV,P_SCH,-1},{90,120,45,60,35}},
{"TRANCE",{P_OW,P_L0D,P_L0S,P_L0P,P_E0A,P_E0R,-1},{2,P_F1C,25,35,10,60}},
{"CHIP",{P_OA,P_OW,P_FR2,P_E0A,P_E0D,P_E0S,-1},{1,0,3,1,12,0}},
{"SUBBASS",{P_OW,P_OS,P_OO,P_E0A,P_E0R,-1},{0,120,0,5,20}},
{"LEAD",{P_OW,P_E0A,P_E0D,P_E0S,P_E0R,P_F1E,-1},{4,8,20,80,25,45}},
{"SOFTPAD",{P_OD,P_E0A,P_E0R,P_F1C,P_SRV,-1},{30,80,110,60,40}},
{"KEYS",{P_OW,P_E0A,P_E0D,P_E0S,P_E0R,P_F1C,-1},{1,6,35,25,40,65}},
{"BELL2",{P_OA,P_OW,P_FR2,P_FR1,P_FE1,P_E0A,P_E0D,P_E0R},{1,5,13,5,20,0,60,80}},
{"ORGAN",{P_OW,P_OS,P_E0A,P_E0S,P_E0R,-1},{0,60,0,120,5}},
{"STRINGS",{P_OD,P_E0A,P_E0D,P_E0S,P_E0R,P_F1C,-1},{20,50,30,100,70,75}},
{"DRONE2",{P_OD,P_OS,P_E0A,P_E0R,P_L0D,P_L0P,-1},{70,100,100,100,P_OW,20}},
{"PERC",{P_OW,P_E0A,P_E0D,P_E0S,P_E0R,P_F1T,-1},{5,0,12,0,12,2}},
{"ACID2",{P_F1C,P_F1R,P_F1E,P_E1D,P_E1S,P_E0S,P_E0R,-1},{45,95,115,30,5,90,12}},
{"FMKEYS",{P_OA,P_OW,P_FR2,P_FE1,P_E0A,P_E0D,P_E0R,-1},{1,2,7,25,4,35,35}}};

/* Пользовательский пресет = 114 байт звука дорожки (всё, кроме NOTE/EUCLID/FX/глобалов) в этом порядке. */
static const uint8_t PRS_IDS[PRS_NVALS]={
  P_OA, P_OW, P_OP, P_OT, P_OO, P_OD, P_OS, P_OL,
  P_OW2, P_OP2, P_OT2, P_OO2, P_OD2, P_OS2, P_OL2, P_OPI,
  P_OPI2, P_FR1, P_FF1, P_FE1, P_FV1, P_FR2, P_FF2, P_FE2,
  P_FV2, P_ADW, P_ADS, P_APAN, P_AVOL, P_ADR, P_SDL, P_SRV,
  P_SCH, P_TRN, P_F1C, P_F1R, P_F1T, P_F1E, P_F2C, P_F2R,
  P_F2T, P_F2E, P_F1W, P_F2W, P_L0D, P_L0S, P_L0P, P_L0W,
  P_L0M, P_L0Y, P_L0Q, P_L0R, P_L0T, P_L1D, P_L1S, P_L1P,
  P_L1W, P_L1M, P_L1Y, P_L1Q, P_L1R, P_L1T, P_L2D, P_L2S,
  P_L2P, P_L2W, P_L2M, P_L2Y, P_L2Q, P_L2R, P_L2T, P_L3D,
  P_L3S, P_L3P, P_L3W, P_L3M, P_L3Y, P_L3Q, P_L3R, P_L3T,
  P_E0A, P_E0D, P_E0S, P_E0R, P_E1A, P_E1D, P_E1S, P_E1R,
  P_E2A, P_E2D, P_E2S, P_E2R, P_E3A, P_E3D, P_E3S, P_E3R,
  P_E4A, P_E4D, P_E4S, P_E4R, P_E4M, P_E4P, P_E0X, P_E0N,
  P_E0V, P_E1X, P_E1N, P_E1V, P_E2X, P_E2N, P_E2V, P_E3X,
  P_E3N, P_E3V};
const char *worm_preset_factory_name(int i){return PRS[i].n;}
const char *worm_preset_name(int i){static char nm[12];
 if(worm_store_preset_read(i,nm,0))return nm;
 return PRS[i].n;}
/* Глубина модуляции (LFO / free ENV) в долях полного диапазона параметра: обычно линейно (±1 = весь диапазон от базы).
 * PITCH / OCT осцилляторов — музыкально: квадратичная кривая, на полной глубине ±1 октава. Раньше LFO при умолчательной глубине 80
 * уводил PITCH на ±30 полутонов и упирался в границы ±24 (синус превращался в «трапецию-сирену»), а вибрато не настраивалось. */
float worm_mod_span(int id,float m){
 if(id==P_OPI||id==P_OPI2||id==P_OO||id==P_OO2){float a=m<0.f?-m:m;if(a>1.f)a=1.f;a*=a;
  a*=(id==P_OPI||id==P_OPI2)?.25f:(1.f/6.f);   /* PITCH: 48 пт * .25 = 12 полутонов; OCT: 6 окт / 6 = 1 октава */
  return m<0.f?-a:a;}
 return m;}
void worm_patch_default(int16_t *p){for(int i=0;i<NP;i++)p[i]=worm_pinfo[i].def;}

/* Per-track vs глобальные параметры (см. worm.h). Per-track — звук дорожки:
 * NOTE TRIG (P_NR..P_NPO), OSC (P_OA..P_OL2), AMP (P_ADW..P_ADR), SENDS (P_SDL..P_SCH),
 * FLT (P_F1C..P_F2W), LFO (P_L0D..P_L3T), ENV (P_E0A..P_E2V).
 * Глобальные: FX-процессор (P_XT..P_CMX) и всё от P_GSC (scale/root/.../arp/master).
 * NOTE: W.p остаётся «рабочим» патчем (редактируемая дорожка + глобальные FX/мастер);
 * звук каждого трека хранится в W.tr[t].p (см. worm_seq.c: select_track/trk_sync). */
int worm_param_is_track(int id)
{
    return (id >= P_NR  && id <= P_NPO) ||   /* NOTE TRIG */
           (id >= P_OA  && id <= P_FV2) ||   /* OSC1/OSC2 (+PITCH, +FM-параметры) */
           (id >= P_WH1 && id <= P_WM1) ||   /* WT-параметры (HARMON/FOLD/MORPH) */
           (id >= P_ADW && id <= P_ADR) ||   /* AMP */
           (id >= P_SDL && id <= P_SCH) ||   /* SENDS */
           (id >= P_F1C && id <= P_F2W) ||   /* FLT1/FLT2 */
           (id >= P_L0D && id < P_L0D+NLFO*9) ||   /* LFO1..NLFO */
           (id >= P_E0A && id <= P_E3V) ||   /* ADSR: E0 amp OSC1, E1/E2 фильтры, E3 amp OSC2 */
           id == P_TRN ||                    /* TRANSIENT — у каждой дорожки свой */
           (id >= P_EUH && id <= P_EUM) ||   /* EUCLID (ритм канала) */
           id == P_RS1;                       /* зарезервировано (бывший VOICES): слот остаётся, формат банков не меняется */ 
}void worm_preset_load(int16_t *p,int i){int16_t g[NP];uint8_t v[PRS_NVALS];memcpy(g,p,sizeof g);
 int user=!worm_prs_busy&&worm_store_preset_read(i,0,v);
 worm_patch_default(p);
 if(user){for(int k=0;k<PRS_NVALS;k++)p[PRS_IDS[k]]=(int8_t)v[k];}
 else{for(int k=0;k<8&&PRS[i].id[k]>=0;k++)if(PRS[i].id[k]==P_OA&&PRS[i].v[k]==ALGO_FM)worm_fm_defaults(p);   /* слоты OSC у FM значат другое */
  for(int k=0;k<8&&PRS[i].id[k]>=0;k++){int id=PRS[i].id[k],x=PRS[i].v[k];if(id==P_L0D)x=worm_dest_index(x);   /* в пресете DEST задан id параметра */
   p[id]=(int16_t)x;}}
 for(int k=P_GSC;k<NP;k++)p[k]=g[k];
 for(int k=P_XT;k<=P_CMX;k++)p[k]=g[k];}   /* общие FX (delay/reverb/chorus) пресет не трогает */
int worm_preset_save_cur(int i){uint8_t v[PRS_NVALS];char nm[12];worm_trk_sync();   /* рабочая копия W.p -> W.tr[]: иначе пресет мог уйти без последних правок */
 const char *name=worm_store_preset_read(i,nm,0)?nm:PRS[i].n;
 const int16_t *p=W.tr[W.pat].p;
 for(int k=0;k<PRS_NVALS;k++)v[k]=(uint8_t)(int8_t)p[PRS_IDS[k]];
 return worm_store_preset_save(i,name,v);}
void worm_fmt(int id,int v,char *o){const pinfo_t *pi=&worm_pinfo[id];
 switch(pi->fmt){case F_PCT:sprintf(o,"%d%%",(v*100+63)/127);break;case F_SG:v-=64;/*fall*/case F_SGN:sprintf(o,"%+d",v);break;
 case F_LIST:if(v<pi->mn)v=pi->mn;if(v>pi->mx)v=pi->mx;strcpy(o,pi->list[v]);break;   /* защита от значений вне списка (старые проекты/PKS) */
 case F_FRT:{int m=(int)(exp2f((v-64)*(1.f/32.f))*100.f+.5f);sprintf(o,"x%d.%02d",m/100,m%100);break;}case F_NOTE:{static const char*N[]={"C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};sprintf(o,"%s%d",N[v%12],v/12);break;}
 case F_HZ:{float f=20.f*expf(v*(6.9077553f/127.f));   /* как у синта: 20 Гц..20 кГц */if(f>=1000)sprintf(o,"%d.%dk",(int)(f/1000),(int)(f/100)%10);else sprintf(o,"%d",(int)f);break;}
 case F_LHZ:{int h=(int)((.1f+v/127.f*19.9f)*10);sprintf(o,"%d.%dHz",h/10,h%10);break;}
 case F_CRT:{int h=(int)((v/20.f+.1f)*10);sprintf(o,"%d.%dHz",h/10,h%10);break;}
 case F_PGM:if(v<0)strcpy(o,"OFF");else sprintf(o,"%d",v+1);break;   /* PROG: 1..128 (шлётся 0..127) */
 case F_FM:{int k=worm_fm_of(id);if(k>=0)worm_fm_fmt(k,v,o);else sprintf(o,"%d",v);break;}
 case F_OFFN:if(v<0)strcpy(o,"OFF");else sprintf(o,"%d",v);break;
 case F_PAN:if(v==64)strcpy(o,"C");else if(v<64)sprintf(o,"L%d",64-v);else sprintf(o,"R%d",v-64);break;
 default:sprintf(o,"%d",v);}}

/* ---- LEN: индекс <-> длина ---- */
static const uint8_t LEN_SUB[8]={1,2,4,6,8,10,12,14};   /* доли шага в 1/16: 0.063 0.125 0.25 0.375 0.5 0.625 0.75 0.875 (подписи как у Elektron: 0.125.., 1 шаг = 1/16, 2 = 1/8, 4 = 1/4, 8 = 1/2) */
int worm_len_x16(int idx){if(idx<0)idx=0;if(idx>NLEN-1)idx=NLEN-1;return idx<8?LEN_SUB[idx]:(idx-7)*16;}
uint32_t worm_len_samples(int idx,uint32_t ss){int x=worm_len_x16(idx);return ss*(uint32_t)(x>>4)+((ss*(uint32_t)(x&15))>>4);}
/* банк: 0..4 — старая шкала (1/2/4/8/16 шагов = индексы 8/9/11/15/23), остальное — idx+5 (0..4 не пересекается) */
int worm_len_store(int idx){static const uint8_t lg[5]={8,9,11,15,23};if(idx<0)idx=0;if(idx>NLEN-1)idx=NLEN-1;
 for(int i=0;i<5;i++)if(lg[i]==idx)return i;return idx+5;}
int worm_len_load(int sv){static const uint8_t lg[5]={8,9,11,15,23};if(sv<0)return LEN_DEF;if(sv<5)return lg[sv];sv-=5;return sv>NLEN-1?NLEN-1:sv;}
/* микротайминг: pct% шага 1x -> 0.1 мс (шаг 1x = 15 / BPM секунды) */
int worm_mt_ms10(int pct,float bpm){if(bpm<1.f)bpm=1.f;return (int)(((float)pct*1500.f)/bpm+(pct<0?-.5f:.5f));}
