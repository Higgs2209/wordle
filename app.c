#include "../../Pala_One_2_1/pala_app.h"
#include "../../Pala_One_2_1/pala_api.h"

__attribute__((section(".header")))
const PalaAppHeader pala_header = {
    .magic        = PALA_APP_MAGIC,
    .api_version  = PALA_API_VERSION,
    .name         = "Wordle",
    .entry_offset = 0,
    .reloc_offset = 0,
    .reloc_count  = 0,
};

#define LONG_PRESS_MS  850u
#define DAY_SECS       86400u

#define NUM_GUESSES    6
#define WORD_LEN       5

#define STATUS_PLAY    0
#define STATUS_WON     1
#define STATUS_LOST    2

/* Cell render states */
#define CELL_EMPTY  0   /* unused future cell ('_') */
#define CELL_DRAFT  1   /* committed letter in current row */
#define CELL_CURSOR 2   /* current editing position with candidate letter */
#define CELL_RIGHT  3   /* past guess: correct letter, correct position */
#define CELL_WRONG  4   /* past guess: correct letter, wrong position */
#define CELL_MISS   5   /* past guess: letter not in word */

#define UNSET_DAY  0xFFFFFFFFu

/* Daily word = WORDS[dayIdx % NUM_WORDS]. */
static const char WORDS[][WORD_LEN + 1] = {
    "ABOUT","ABOVE","ABUSE","ACTOR","ACUTE","ADMIT","ADOPT","ADULT","AFTER","AGAIN",
    "AGREE","AHEAD","ALARM","ALBUM","ALERT","ALIEN","ALIGN","ALIKE","ALIVE","ALLOW",
    "ALONE","ALONG","ALTER","AMBER","ANGEL","ANGER","ANGLE","ANGRY","APART","APPLE",
    "APPLY","ARENA","ARGUE","ARISE","ARRAY","ASIDE","ASSET","AUDIO","AWAKE","AWARD",
    "AWARE","BADGE","BASIC","BASIS","BEACH","BEGIN","BEING","BENCH","BLACK","BLAME",
    "BLEND","BLESS","BLIND","BLOCK","BLOOD","BOARD","BRAIN","BRAND","BRAVE","BREAD",
    "BREAK","BRIEF","BRING","BROAD","BROWN","BRUSH","BUILD","BUILT","BUNCH","BUYER",
    "CABIN","CABLE","CARRY","CATCH","CAUSE","CHAIN","CHAIR","CHARM","CHART","CHASE",
    "CHEAP","CHECK","CHIEF","CHILD","CIVIL","CLAIM","CLASS","CLEAN","CLEAR","CLICK",
    "CLIFF","CLIMB","CLOCK","CLOSE","CLOUD","COACH","COAST","COVER","CRAFT","CRASH",
    "CREAM","CRIME","CROSS","CROWD","CROWN","CURVE","DAILY","DANCE","DEATH","DELAY",
    "DEPTH","DOZEN","DRAFT","DRAMA","DREAM","DRESS","DRINK","DRIVE","EAGER","EARLY",
    "EARTH","EIGHT","EMPTY","ENEMY","ENJOY","ENTER","ENTRY","EQUAL","EVENT","EVERY",
    "EXACT","EXIST","EXTRA","FAITH","FALSE","FAULT","FIELD","FIFTH","FIFTY","FIGHT",
    "FINAL","FIRST","FLAME","FLASH","FLEET","FLESH","FLOAT","FLOOR","FOCUS","FORCE",
    "FORTH","FORTY","FOUND","FRAME","FRESH","FRONT","FRUIT","FUNNY","GHOST","GIANT",
    "GLASS","GLOBE","GLORY","GRACE","GRADE","GRAIN","GRAND","GRANT","GRAPE","GRASS",
    "GRAVE","GREAT","GREEN","GROUP","GUARD","HAPPY","HEART","HEAVY","HOTEL","HOUSE",
    "HUMAN","IDEAL","IMAGE","INDEX","INNER","INPUT","ISSUE","JOINT","JUDGE","JUICE",
    "KNIFE","LARGE","LAUGH","LEARN","LEAST","LEAVE","LEGAL","LEVEL","LIGHT","LIMIT",
    "LOCAL","LOOSE","LOWER","LUCKY","MAGIC","MAJOR","MAYBE","MEDIA","METAL","MIGHT",
    "MINOR","MIXED","MODEL","MONEY","MONTH","MOUSE","MOUTH","MOVIE","MUSIC","NEVER",
    "NIGHT","NOBLE","NOISE","NORTH","NOVEL","NURSE","OCCUR","OCEAN","OFFER","OFTEN",
    "ORDER","ORGAN","OTHER","OUNCE","OUTER","OWNER","PAINT","PANEL","PAPER","PARTY",
    "PEACE","PHASE","PHONE","PIANO","PIECE","PILOT","PITCH","PIZZA","PLACE","PLAIN",
    "PLANE","PLANT","PLATE","POINT","POUND","POWER","PRESS","PRICE","PRIDE","PRIME",
    "PRINT","PRIZE","PROOF","PROUD","PROVE","PURSE","QUEEN","QUERY","QUEST","QUICK",
    "QUIET","QUITE","RADIO","RAISE","RANGE","RAPID","RATIO","REACH","READY","REFER",
    "RELAY","REPLY","RESET","RIVER","ROBOT","ROUGH","ROUND","ROYAL","RURAL","SAINT",
    "SALAD","SAUCE","SCALE","SCARE","SCENE","SCOPE","SCORE","SCOUT","SEVEN","SHADE",
    "SHALL","SHAPE","SHARE","SHARP","SHEEP","SHEET","SHELF","SHELL","SHIFT","SHINE",
    "SHIRT","SHOCK","SHOOT","SHORE","SHORT","SIGHT","SINCE","SIXTH","SIXTY","SKILL",
    "SLEEP","SLIDE","SMALL","SMART","SMILE","SMOKE","SOLID","SOLVE","SORRY","SOUND",
    "SOUTH","SPACE","SPARE","SPEAK","SPEED","SPEND","SPLIT","SPORT","STAGE","STAND",
    "START","STATE","STEAM","STEEL","STICK","STILL","STOCK","STONE","STORE","STORM",
    "STORY","STYLE","SUGAR","SUITE","SUPER","SWEET","SWIFT","TABLE","TASTE","TEACH",
    "TEETH","THANK","THEFT","THEIR","THEME","THERE","THESE","THICK","THING","THINK",
    "THIRD","THOSE","THREE","THREW","THROW","TIGHT","TIRED","TITLE","TODAY","TOPIC",
    "TOTAL","TOUCH","TOUGH","TOWER","TRACE","TRACK","TRADE","TRAIL","TRAIN","TREAT",
    "TREND","TRIAL","TRIBE","TRICK","TRULY","TRUST","TRUTH","TWICE","UNCLE","UNDER",
    "UNION","UNITE","UNITY","UNTIL","UPPER","UPSET","URBAN","USAGE","USUAL","VAGUE",
    "VALID","VALUE","VIDEO","VIRUS","VISIT","VITAL","VOCAL","VOICE","WAGON","WASTE",
    "WATCH","WATER","WHEAT","WHEEL","WHERE","WHICH","WHILE","WHITE","WHOLE","WHOSE",
    "WIDOW","WOMAN","WORLD","WORRY","WORSE","WORTH","WOUND","WRITE","WRONG","YOUNG",
};
#define NUM_WORDS ((int)(sizeof(WORDS) / sizeof(WORDS[0])))

/* Cell column x positions (14 px per cell, centered: (250 - 5*14) / 2 = 90).
   Decorated cells render as `<X>` or `[X]` with `<`/`[` at x, letter at x+5 */
static const int CELL_X[WORD_LEN]   = {90, 104, 118, 132, 146};
static const int ROW_Y[NUM_GUESSES] = {30, 42, 54, 66, 78, 90};

typedef struct {
    uint32_t version;        /* schema version */
    uint32_t dayIdx;         /* day index this daily state belongs to */
    uint32_t status;         /* STATUS_PLAY / WON / LOST */
    uint32_t guessRow;       /* number of fully submitted guesses (next row index) */
    uint32_t curPos;         /* current edit position within active row (0..WORD_LEN) */
    uint32_t curLetter;      /* current candidate letter index 0..25 */
    char     guesses[NUM_GUESSES][WORD_LEN];
    /* All-time stats, never reset on new day */
    uint32_t played;
    uint32_t won;
    uint32_t streak;
    uint32_t maxStreak;
    uint32_t lastWonDay;
    uint32_t distrib[NUM_GUESSES];
} SavedState;

static char idxToChar(uint32_t i) {
    return (char)('A' + (int)i);
}

/* Standard Wordle scoring */
static void scoreGuess(const char* guess, const char* target, int* fb) {
    int used[WORD_LEN];
    used[0] = 0; used[1] = 0; used[2] = 0; used[3] = 0; used[4] = 0;
    for (int i = 0; i < WORD_LEN; i++) {
        if (guess[i] == target[i]) {
            fb[i] = 2;
            used[i] = 1;
        } else {
            fb[i] = 0;
        }
    }
    for (int i = 0; i < WORD_LEN; i++) {
        if (fb[i] == 2) continue;
        for (int j = 0; j < WORD_LEN; j++) {
            if (!used[j] && guess[i] == target[j]) {
                fb[i] = 1;
                used[j] = 1;
                break;
            }
        }
    }
}

static void drawCell(const PalaAPI* api, int x, int y, int state, char letter) {
    char buf[2];
    buf[1] = 0;
    if (state == CELL_EMPTY) {
        buf[0] = '_';
        api->drawTextAt(x + 5, y, buf, 0);
    } else if (state == CELL_DRAFT) {
        buf[0] = letter;
        api->drawTextAt(x + 5, y, buf, 1);
    } else if (state == CELL_CURSOR) {
        buf[0] = '<';
        api->drawTextAt(x,      y, buf, 0);
        buf[0] = letter;
        api->drawTextAt(x + 5,  y, buf, 1);
        buf[0] = '>';
        api->drawTextAt(x + 10, y, buf, 0);
    } else if (state == CELL_RIGHT) {
        buf[0] = '[';
        api->drawTextAt(x,      y, buf, 0);
        buf[0] = letter;
        api->drawTextAt(x + 5,  y, buf, 1);
        buf[0] = ']';
        api->drawTextAt(x + 10, y, buf, 0);
    } else if (state == CELL_WRONG) {
        buf[0] = letter;
        api->drawTextAt(x + 5, y, buf, 1);
    } else {  /* CELL_MISS */
        buf[0] = '.';
        api->drawTextAt(x + 6, y, buf, 0);
    }
}

static int cellStateForFb(int fb) {
    if (fb == 2) return CELL_RIGHT;
    if (fb == 1) return CELL_WRONG;
    return CELL_MISS;
}

static void drawBoard(const PalaAPI* api, const SavedState* s, const char* target) {
    char hdrBuf[24];
    api->clearScreen();
    api->snprintf_wrap(hdrBuf, sizeof(hdrBuf), "Wordle Day %u", (unsigned)s->dayIdx);
    api->drawHeader(hdrBuf);

    int fb[WORD_LEN];
    for (int r = 0; r < NUM_GUESSES; r++) {
        if (r < (int)s->guessRow) {
            scoreGuess(s->guesses[r], target, fb);
            for (int c = 0; c < WORD_LEN; c++) {
                drawCell(api, CELL_X[c], ROW_Y[r],
                         cellStateForFb(fb[c]), s->guesses[r][c]);
            }
        } else if (r == (int)s->guessRow && s->status == STATUS_PLAY) {
            for (int c = 0; c < WORD_LEN; c++) {
                if (c < (int)s->curPos) {
                    drawCell(api, CELL_X[c], ROW_Y[r], CELL_DRAFT,
                             s->guesses[r][c]);
                } else if (c == (int)s->curPos) {
                    drawCell(api, CELL_X[c], ROW_Y[r], CELL_CURSOR,
                             idxToChar(s->curLetter));
                } else {
                    drawCell(api, CELL_X[c], ROW_Y[r], CELL_EMPTY, '_');
                }
            }
        } else {
            for (int c = 0; c < WORD_LEN; c++) {
                drawCell(api, CELL_X[c], ROW_Y[r], CELL_EMPTY, '_');
            }
        }
    }

    /* Status line */
    char stBuf[32];
    if (s->status == STATUS_PLAY) {
        api->snprintf_wrap(stBuf, sizeof(stBuf), "Guess %u of %u",
                           (unsigned)(s->guessRow + 1), NUM_GUESSES);
    } else {
        api->snprintf_wrap(stBuf, sizeof(stBuf), "Streak %u  Best %u",
                           (unsigned)s->streak, (unsigned)s->maxStreak);
    }
    api->drawTextAt(8, 104, stBuf, 0);

    /* Footer hint */
    char ftBuf[40];
    if (s->status == STATUS_PLAY) {
        api->drawTextAt(4, 118, "1=next 2=prev 3=send  Hold=exit", 0);
    } else if (s->status == STATUS_WON) {
        api->snprintf_wrap(ftBuf, sizeof(ftBuf), "Won in %u! Hold=exit",
                           (unsigned)s->guessRow);
        api->drawTextAt(4, 118, ftBuf, 1);
    } else {
        api->snprintf_wrap(ftBuf, sizeof(ftBuf), "Word: %s  Hold=exit", target);
        api->drawTextAt(4, 118, ftBuf, 1);
    }

    api->refreshDisplay();
}

/* Zero out the daily grid one character at a time, via a volatile pointer so
   the compiler doesn't lower the loop into a memset call */
static void clearGuesses(char g[NUM_GUESSES][WORD_LEN]) {
    volatile char* p = (volatile char*)g;
    for (int i = 0; i < NUM_GUESSES * WORD_LEN; i++) p[i] = ' ';
}

static void initFreshState(SavedState* s, uint32_t dayIdx) {
    s->version    = 1;
    s->dayIdx     = dayIdx;
    s->status     = STATUS_PLAY;
    s->guessRow   = 0;
    s->curPos     = 0;
    s->curLetter  = 0;
    s->played     = 0;
    s->won        = 0;
    s->streak     = 0;
    s->maxStreak  = 0;
    s->lastWonDay = UNSET_DAY;
    s->distrib[0] = 0; s->distrib[1] = 0; s->distrib[2] = 0;
    s->distrib[3] = 0; s->distrib[4] = 0; s->distrib[5] = 0;
    clearGuesses(s->guesses);
}

static void resetDailyState(SavedState* s, uint32_t dayIdx) {
    s->dayIdx    = dayIdx;
    s->status    = STATUS_PLAY;
    s->guessRow  = 0;
    s->curPos    = 0;
    s->curLetter = 0;
    clearGuesses(s->guesses);
}

void app_main(const PalaAPI* api) {
    SavedState s;
    int hasSave = (api->storageRead("wordle", &s, sizeof(s)) == (int)sizeof(s));

    uint32_t dayIdx = api->rtcSeconds() / DAY_SECS;

    if (!hasSave || s.version != 1) {
        initFreshState(&s, dayIdx);
    } else if (s.dayIdx != dayIdx) {
        resetDailyState(&s, dayIdx);
    } else {
        /* Defensive clamp on resume in case a save got corrupted. */
        if (s.curLetter >= 26)               s.curLetter = 0;
        if (s.curPos    >  WORD_LEN)         s.curPos    = 0;
        if (s.guessRow  >  NUM_GUESSES)      s.guessRow  = 0;
        if (s.status    >  STATUS_LOST)      s.status    = STATUS_PLAY;
    }

    /* Daily target word, stable for the same dayIdx. */
    uint32_t widx = dayIdx;
    while (widx >= (uint32_t)NUM_WORDS) widx -= (uint32_t)NUM_WORDS;
    const char* target = WORDS[widx];

    api->storageWrite("wordle", &s, sizeof(s));

    int needsRedraw    = 1;
    uint32_t pressStart = 0;

    while (1) {
        uint32_t mNow = api->millisNow();

        /* Long-press exit (works in any state). */
        if (api->buttonPressed()) {
            if (pressStart == 0) pressStart = mNow;
            if ((mNow - pressStart) >= LONG_PRESS_MS) {
                api->storageWrite("wordle", &s, sizeof(s));
                return;
            }
        } else {
            pressStart = 0;
        }

        uint8_t evt = api->pollEvent();

        if (s.status == STATUS_PLAY) {
            if (evt == PALA_CLICK) {
                s.curLetter += 1;
                if (s.curLetter >= 26) s.curLetter = 0;
                needsRedraw = 1;
            } else if (evt == PALA_DOUBLE) {
                if (s.curLetter == 0) s.curLetter = 25;
                else                  s.curLetter -= 1;
                needsRedraw = 1;
            } else if (evt == PALA_TRIPLE) {
                /* Commit current candidate at cursor and advance. */
                s.guesses[s.guessRow][s.curPos] = idxToChar(s.curLetter);
                s.curPos    += 1;
                s.curLetter  = 0;

                if (s.curPos >= WORD_LEN) {
                    /* Row full -> submit guess. */
                    int isMatch = 1;
                    for (int c = 0; c < WORD_LEN; c++) {
                        if (s.guesses[s.guessRow][c] != target[c]) {
                            isMatch = 0;
                            break;
                        }
                    }
                    s.guessRow += 1;
                    s.curPos    = 0;

                    if (isMatch) {
                        s.status  = STATUS_WON;
                        s.played += 1;
                        s.won    += 1;
                        s.distrib[s.guessRow - 1] += 1;
                        if (s.lastWonDay != UNSET_DAY && s.lastWonDay + 1 == dayIdx) {
                            s.streak += 1;
                        } else {
                            s.streak = 1;
                        }
                        if (s.streak > s.maxStreak) s.maxStreak = s.streak;
                        s.lastWonDay = dayIdx;
                    } else if (s.guessRow >= NUM_GUESSES) {
                        s.status  = STATUS_LOST;
                        s.played += 1;
                        s.streak  = 0;
                    }
                }
                api->storageWrite("wordle", &s, sizeof(s));
                needsRedraw = 1;
            }
        }
        /* In WON/LOST: clicks do nothing. Long-press exits. */

        if (needsRedraw) {
            drawBoard(api, &s, target);
            needsRedraw = 0;
        }

        api->delayMs(10);
    }
}
