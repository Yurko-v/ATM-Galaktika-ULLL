#pragma once

namespace L
{
    const int CAPTION_H = 17;
    const int CAP_GAP   = 5;
    const int BLOCK_GAP = 6;
    const int BLOCK_GAP_WIDE = 8;
    const int NOCAP_GAP = 16;

    const int CLOCK_H   = 24;
    const int DATE_H    = 18;

    const int HDR_SPACE  = 16;
    const int CLOCK_LEAD = (CLOCK_H - 20) / 2;
    const int DATE_LEAD  = (DATE_H - 14) / 2;
    const int CAP_LEAD   = (CAPTION_H - 14) / 2;

    const int HDR_TOP   = HDR_SPACE - CLOCK_LEAD;
    const int CLOCK_GAP = HDR_SPACE - CLOCK_LEAD - DATE_LEAD;
    const int HDR_GAP   = HDR_SPACE - DATE_LEAD - CAP_LEAD;
    const int HEADER_H  = HDR_TOP + CLOCK_H + CLOCK_GAP + DATE_H;

    const int T_PAD = 4, T_ROW = 20;
    const int TIMER_BOX_H = T_PAD + T_ROW + T_PAD;

    const int U_TOP = 4, U_ROW = 20, U_GAP = 5, U_BOT = 4;
    const int USER_BOX_H = U_TOP + U_ROW + U_GAP + U_ROW + U_BOT;

    const int V_TOP = 5, V_ROW = 20, V_GAP1 = 8, V_CHK = 16, V_GAP2 = 6, V_BOT = 6;
    const int VECTORS_BOX_H = V_TOP + V_ROW + V_GAP1 + V_CHK + V_GAP2 + V_CHK + V_BOT;

    const int O_TOP = 4, O_LABEL = V_ROW, O_GAP = 4, O_LIST_H = 130, O_BOT = 4;
    const int OS_ROW = 16, OS_PITCH = 21, OS_ROW0 = 4;
    const int OS_BOX_H = O_TOP + O_LABEL + O_GAP + O_LIST_H + O_BOT;

    const int E_TOP = 4, E_ROW = 16, E_PITCH = 20, E_BOT = 6;
    const int UNITS_BOX_H = E_TOP + 3 * E_PITCH + E_ROW + E_BOT;

    const int F_TOP = 4, F_ROW = 17, F_GAP1 = 5, F_GAP2 = 9, F_BOT = 4;
    const int ALTFILTER_BOX_H = F_TOP + F_ROW + F_GAP1 + F_ROW + F_GAP2 + F_ROW + F_BOT;

    const int C_ROW_H    = 15;
    const int C_CAP_H    = 15;
    const int C_FIELD_H  = 16;
    const int C_VV_TOP     = 2,  C_VV_H    = 17;
    const int C_ALL_TOP    = 2;
    const int C_BP_TOP     = 19;
    const int C_FLT_TOP    = 19, C_FLT_H   = 15;
    const int C_EXTRA_TOP  = 20, C_EXTRA_H = 13;
    const int C_SLIDER_TOP = 32, C_SLIDER_BOT = 105;
    const int C_DISTRESS_CAP = 36, C_DISTRESS_FIELD = 53;
    const int C_DUP_CAP      = 74, C_DUP_FIELD      = 91;
    const int CODES_BOX_H = 113;

    const int A_TOP = 3, A_ROW = 21, A_GAP = 5, A_BOT = 3;
    const int AERODROME_BOX_H = A_TOP + A_ROW + A_GAP + A_ROW + A_BOT;

    const int AU_TOP = 4, AU_ROW = 20, AU_GAP = 5, AU_GAP2 = 8, AU_STATUS = 20, AU_BOT = 6;
    const int AUTH_BOX_H      = AU_TOP + AU_ROW + AU_GAP + AU_ROW + AU_GAP2 + AU_STATUS + AU_BOT;
    const int AUTH_BOX_H_IDLE = AU_TOP + AU_ROW + AU_GAP + AU_ROW + AU_BOT;

    const int MENU_BAR_H = 28;

    const int PANEL_BOT_PAD = 8;
    const int COLLAPSED_BOT_PAD = 6;

    inline int Block(int boxH) { return CAPTION_H + CAP_GAP + boxH; }
}
