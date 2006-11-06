/*
 *  File:       initfile.cc
 *  Summary:    Simple reading of an init file and system variables
 *  Written by: David Loewenstern
 *
 *  Modified for Crawl Reference by $Author$ on $Date$
 *
 *  Change History (most recent first):
 *
 *      <3>     5 May 2000      GDL             Add field stripping for 'name'
 *      <2>     6/12/99         BWR             Added get_system_environment
 *      <1>     6/9/99          DML             Created
 */

#include "AppHdr.h"
#include "externs.h"

#include "initfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <ctype.h>

#include "clua.h"
#include "delay.h"
#include "Kills.h"
#include "files.h"
#include "defines.h"
#include "libutil.h"
#include "player.h"
#include "stash.h"
#include "stuff.h"
#include "travel.h"
#include "items.h"
#include "view.h"

game_options    Options;

extern bool autopickup_on;

std::string &tolower_string( std::string &str );

const static char *obj_syms = ")([/%.?=!.+\\0}X$";
const static int   obj_syms_len = 16;

static void read_startup_prefs();
static void read_options(InitLineInput &il, bool runscript);

template<class A, class B> void append_vector(A &dest, const B &src)
{
    dest.insert( dest.end(), src.begin(), src.end() );
}

// returns -1 if unmatched else returns 0-15
int str_to_colour( const std::string &str, int default_colour )
{
    int ret;

    const std::string cols[16] =
    {
        "black", "blue", "green", "cyan", "red", "magenta", "brown",
        "lightgrey", "darkgrey", "lightblue", "lightgreen", "lightcyan",
        "lightred", "lightmagenta", "yellow", "white"
    };

    const std::string element_cols[] =
    {
        "fire", "ice", "earth", "electricity", "air", "poison", "water",
        "magic", "mutagenic", "warp", "enchant", "heal", "holy", "dark",
        "necro", "necro", "unholy", "vehumet", "crystal", "blood", "smoke",
        "slime", "jewel", "elven", "dwarven", "orcish", "gila", "floor",
        "rock", "stone", "random"
    };

    for (ret = 0; ret < 16; ret++)
    {
        if (str == cols[ret])
            break;
    }

    // check for alternate spellings
    if (ret == 16)
    {
        if (str == "lightgray")
            ret = 7;
        else if (str == "darkgray")
            ret = 8;
    }

    if (ret == 16)
    {
        // Maybe we have an element colour attribute.
        for (unsigned i = 0; i < sizeof(element_cols) / sizeof(*element_cols); 
                ++i)
        {
            if (str == element_cols[i])
            {
                // Ugh.
                ret = element_type(EC_FIRE + i);
                break;
            }
        }
    }

    if (ret == 16)
    {
        // Check if we have a direct colour index
        const char *s = str.c_str();
        char *es = NULL;
        int ci = (int) strtol(s, &es, 10);
        if (s != (const char *) es && es && ci >= 0 && ci < 16)
            ret = ci;
    }

    return ((ret == 16) ? default_colour : ret);
}

// returns -1 if unmatched else returns 0-15
static int str_to_channel_colour( const std::string &str )
{
    int ret = str_to_colour( str );

    if (ret == -1)
    {
        if (str == "mute")
            ret = MSGCOL_MUTED;
        else if (str == "plain" || str == "off")
            ret = MSGCOL_PLAIN;
        else if (str == "default" || str == "on")
            ret = MSGCOL_DEFAULT;
        else if (str == "alternate")
            ret = MSGCOL_ALTERNATE;
    }

    return (ret);
}

static const std::string message_channel_names[ NUM_MESSAGE_CHANNELS ] =
{
    "plain", "prompt", "god", "pray", "duration", "danger", "warning", "food",
    "recovery", "sound", "talk", "intrinsic_gain", "mutation", "monster_spell",
    "monster_enchant", "monster_damage", "monster_target", 
    "rotten_meat", "equipment", "floor", "multiturn", "diagnostic",
};

// returns -1 if unmatched else returns 0--(NUM_MESSAGE_CHANNELS-1)
int str_to_channel( const std::string &str )
{
    int ret;

    for (ret = 0; ret < NUM_MESSAGE_CHANNELS; ret++)
    {
        if (str == message_channel_names[ret])
            break;
    }

    return (ret == NUM_MESSAGE_CHANNELS ? -1 : ret);
}

std::string channel_to_str( int channel )
{
    if (channel < 0 || channel >= NUM_MESSAGE_CHANNELS)
        return "";

    return message_channel_names[channel];
}

static int str_to_book( const std::string& str )
{
    if ( str == "fire" || str == "flame" )
	return SBT_FIRE;
    if ( str == "cold" || str == "ice" )
	return SBT_COLD;
    if ( str == "summ" || str == "summoning" )
	return SBT_SUMM;
    if ( str == "random" )
	return SBT_RANDOM;
    return SBT_NO_SELECTION;
}

static int str_to_weapon( const std::string &str )
{
    if (str == "shortsword" || str == "short sword")
        return (WPN_SHORT_SWORD);
    else if (str == "mace")
        return (WPN_MACE);
    else if (str == "spear")
        return (WPN_SPEAR);
    else if (str == "trident")
        return (WPN_TRIDENT);
    else if (str == "hand axe" || str == "handaxe")
        return (WPN_HAND_AXE);
    else if (str == "random")
        return (WPN_RANDOM);

    return (WPN_UNKNOWN);
}

static std::string weapon_to_str( int weapon )
{
    switch (weapon)
    {
    case WPN_SHORT_SWORD:
        return "short sword";
    case WPN_MACE:
        return "mace";
    case WPN_SPEAR:
        return "spear";
    case WPN_TRIDENT:
        return "trident";
    case WPN_HAND_AXE:
        return "hand axe";
    case WPN_RANDOM:
        return "random";
    default:
        return "random";
    }
}

static unsigned int str_to_fire_types( const std::string &str )
{
    if (str == "launcher")
        return (FIRE_LAUNCHER);
    else if (str == "dart")
        return (FIRE_DART);
    else if (str == "stone")
        return (FIRE_STONE);
    else if (str == "dagger")
        return (FIRE_DAGGER);
    else if (str == "spear")
        return (FIRE_SPEAR);
    else if (str == "hand axe" || str == "handaxe")
        return (FIRE_HAND_AXE);
    else if (str == "club")
        return (FIRE_CLUB);

    return (FIRE_NONE);
}

static void str_to_fire_order( const std::string &str, 
                               FixedVector< int, NUM_FIRE_TYPES > &list )
{
    int i;
    size_t pos = 0;
    std::string item = "";

    for (i = 0; i < NUM_FIRE_TYPES; i++)
    {
        // get next item from comma delimited list
        const size_t end = str.find( ',', pos );
        item = str.substr( pos, end - pos );
        trim_string( item );

        list[i] = str_to_fire_types( item );

        if (end == std::string::npos)
            break;
        else
            pos = end + 1;
    }
}

static char str_to_race( const std::string &str )
{
    int index = -1;

    if (str.length() == 1)      // old system of using menu letter
        return (str[0]);
    else if (str.length() == 2) // scan abbreviations
        index = get_species_index_by_abbrev( str.c_str() );

    // if we don't have a match, scan the full names
    if (index == -1)
        index = get_species_index_by_name( str.c_str() );

    // skip over the extra draconians here
    if (index > SP_RED_DRACONIAN)
        index -= (SP_CENTAUR - SP_RED_DRACONIAN - 1);

    // SP_HUMAN is at 1, therefore we must subtract one.
    return ((index != -1) ? index_to_letter( index - 1 ) : 0); 
}

static char str_to_class( const std::string &str )
{
    int index = -1;

    if (str.length() == 1)      // old system of using menu letter
        return (str[0]);
    else if (str.length() == 2) // scan abbreviations
        index = get_class_index_by_abbrev( str.c_str() );

    // if we don't have a match, scan the full names
    if (index == -1)
        index = get_class_index_by_name( str.c_str() );

    return ((index != -1) ? index_to_letter( index ) : 0); 
}

std::string & tolower_string( std::string &str )
{
    if (str.length()) 
    {
        for (std::string::iterator cp = str.begin(); cp != str.end(); cp++) 
        {
            *cp = tolower( *cp );
        }
    }

    return (str);
}

static bool read_bool( const std::string &field, bool def_value )
{
    bool ret = def_value;

    if (field == "true" || field == "1" || field == "yes")
        ret = true;

    if (field == "false" || field == "0" || field == "no")
        ret = false;

    return (ret);
}

static unsigned curses_attribute(const std::string &field)
{
    if (field == "standout")               // probably reverses
        return CHATTR_STANDOUT;
    else if (field == "bold")              // probably brightens fg
        return CHATTR_BOLD;
    else if (field == "blink")             // probably brightens bg
        return CHATTR_BLINK;
    else if (field == "underline")
        return CHATTR_UNDERLINE;
    else if (field == "reverse")
        return CHATTR_REVERSE;
    else if (field == "dim")
        return CHATTR_DIM;
    else if (field.find("hi:") == 0 || field.find("hilite:") == 0 ||
             field.find("highlight:") == 0)
    {
        int col = field.find(":");
        int colour = str_to_colour(field.substr(col + 1));
        if (colour == -1)
            fprintf(stderr, "Bad highlight string -- %s\n", field.c_str());
        else
            return CHATTR_HILITE | (colour << 8);          
    }
    else
        fprintf( stderr, "Bad colour -- %s\n", field.c_str() );
    return CHATTR_NORMAL;
}

static void add_dump_fields(const std::string &text)
{
    // Easy; chardump.cc has most of the intelligence.
    append_vector(Options.dump_order, split_string(",", text, true, true));
}

static void reset_startup_options(bool clear_name = true)
{
    if (clear_name)
        you.your_name[0] = 0;
    Options.race                   = 0;
    Options.cls                    = 0;
    Options.weapon                 = WPN_UNKNOWN;
    Options.book                   = SBT_NO_SELECTION;
    Options.random_pick            = false;
    Options.chaos_knight           = GOD_NO_GOD;
    Options.death_knight           = DK_NO_SELECTION;
    Options.priest                 = GOD_NO_GOD;
}

static void set_default_activity_interrupts()
{
    for (int adelay = 0; adelay < NUM_DELAYS; ++adelay)
        for (int aint = 0; aint < NUM_AINTERRUPTS; ++aint)
            Options.activity_interrupts[adelay][aint] = true;

    const char *default_activity_interrupts[] = {
        "interrupt_armour_on = hp_loss, stat, monster_attack",
        "interrupt_armour_off = interrupt_armour_on",
        "interrupt_drop_item = interrupt_armour_on",
        "interrupt_jewellery_on = interrupt_armour_on",
        "interrupt_memorise = interrupt_armour_on",
        "interrupt_butcher = interrupt_armour_on, teleport",
        "interrupt_passwall = interrupt_butcher",
        "interrupt_multidrop = interrupt_butcher",
        "interrupt_macro = interrupt_multidrop",
        "interrupt_travel = interrupt_butcher, statue, hungry, burden, monster",
        "interrupt_run = interrupt_travel, message",
        "interrupt_rest = interrupt_run",

        // Stair ascents/descents cannot be interrupted, attempts to interrupt
        // the delay will just trash all queued delays, including travel.
        "interrupt_ascending_stairs =",
        "interrupt_descending_stairs =",
        "interrupt_uninterruptible =",
        "interrupt_autopickup =",
        NULL
    };

    for (int i = 0; default_activity_interrupts[i]; ++i)
        parse_option_line( default_activity_interrupts[i], false );
}

static void clear_activity_interrupts(FixedVector<bool, NUM_AINTERRUPTS> &eints)
{
    for (int i = 0; i < NUM_AINTERRUPTS; ++i)
        eints[i] = false;
}

static const std::string interrupt_prefix = "interrupt_";
static void set_activity_interrupt(
        FixedVector<bool, NUM_AINTERRUPTS> &eints,
        const std::string &interrupt)
{
    if (interrupt.find(interrupt_prefix) == 0)
    {
        std::string delay_name = interrupt.substr( interrupt_prefix.length() );
        delay_type delay = get_delay(delay_name);
        if (delay == NUM_DELAYS)
        {
            fprintf(stderr, "Unknown delay: %s\n", delay_name.c_str());
            return;
        }
        
        FixedVector<bool, NUM_AINTERRUPTS> &refints =
            Options.activity_interrupts[delay];

        for (int i = 0; i < NUM_AINTERRUPTS; ++i)
            if (refints[i])
                eints[i] = true;

        return;
    }

    activity_interrupt_type ai = get_activity_interrupt(interrupt);
    if (ai == NUM_AINTERRUPTS)
    {
        fprintf(stderr, "Delay interrupt name \"%s\" not recognised.\n",
                interrupt.c_str());
        return;
    }

    eints[ai] = true;
}
 
static void set_activity_interrupt(const std::string &activity_name,
                                   const std::string &interrupt_names,
                                   bool append_interrupts)
{
    delay_type delay = get_delay(activity_name);
    if (delay == NUM_DELAYS)
    {
        fprintf(stderr, "Unknown delay: %s\n", activity_name.c_str());
        return;
    }

    FixedVector<bool, NUM_AINTERRUPTS> &eints =
            Options.activity_interrupts[ delay ];

    if (!append_interrupts)
        clear_activity_interrupts(eints);

    std::vector<std::string> interrupts = split_string(",", interrupt_names);
    for (int i = 0, size = interrupts.size(); i < size; ++i)
        set_activity_interrupt(eints, interrupts[i]);

    eints[AI_FORCE_INTERRUPT] = true;
}

void reset_options(bool clear_name)
{
    set_default_activity_interrupts();

    reset_startup_options(clear_name);
    Options.prev_race = 0;
    Options.prev_cls  = 0;
    Options.prev_ck   = GOD_NO_GOD;
    Options.prev_dk   = DK_NO_SELECTION;
    Options.prev_pr   = GOD_NO_GOD;
    Options.prev_weapon = WPN_UNKNOWN;
    Options.prev_book = SBT_NO_SELECTION;
    Options.prev_randpick = false;
    Options.remember_name = false;

#ifdef USE_ASCII_CHARACTERS
    Options.char_set               = CSET_ASCII;
#else
    Options.char_set               = CSET_IBM;
#endif
    init_char_table(Options.char_set);
    
    // set it to the .crawlrc default
    Options.autopickups = ((1L << 15) | // gold
                           (1L <<  6) | // scrolls
                           (1L <<  8) | // potions
                           (1L << 10) | // books
                           (1L <<  7) | // jewellery
                           (1L <<  3) | // wands
                           (1L <<  4)); // food
    Options.verbose_dump           = false;
    Options.detailed_stat_dump     = true;
    Options.colour_map             = false;
    Options.clean_map              = false;
    Options.show_uncursed          = true;
    Options.always_greet           = false;
    Options.easy_open              = true;
    Options.easy_unequip           = true;
    Options.easy_butcher           = false;
    Options.easy_confirm           = CONFIRM_SAFE_EASY;
    Options.easy_quit_item_prompts = false;
    Options.hp_warning             = 10;
    Options.hp_attention           = 25;
    Options.confirm_self_target    = false;
    Options.safe_autopickup        = false;
    Options.use_notes              = true;
    Options.note_skill_max         = false;
    Options.note_all_spells        = false;
    Options.note_hp_percent        = 0;
    Options.ood_interesting        = 8;
    Options.terse_hand             = true;
    Options.increasing_skill_progress = false;
    Options.auto_list              = false;
    Options.delay_message_clear    = false;
    Options.pickup_dropped         = true;
    Options.travel_colour          = true;
    Options.travel_delay           = 10;
    Options.travel_stair_cost      = 500;
    Options.travel_exclude_radius2 =  68;

    Options.sort_menus             = false;

    Options.tc_reachable           = BLUE;
    Options.tc_excluded            = LIGHTMAGENTA;
    Options.tc_exclude_circle      = RED;
    Options.tc_dangerous           = CYAN;
    Options.tc_disconnected        = DARKGREY;

    Options.show_waypoints         = true;
    Options.item_colour            = true;

    // [ds] Default to jazzy colours.
    Options.detected_item_colour   = LIGHTGREEN;
    Options.detected_monster_colour= LIGHTRED;
    Options.remembered_monster_colour = 0;

    Options.easy_exit_menu         = true;
    Options.dos_use_background_intensity = false;
    Options.assign_item_slot       = SS_FORWARD;

    Options.macro_meta_entry       = true;

    // 10 was the cursor step default on Linux.
    Options.level_map_cursor_step  = 10;

#ifdef STASH_TRACKING
    Options.stash_tracking         = STM_EXPLICIT;
#endif
    Options.explore_stop           = ES_ITEM | ES_STAIR | ES_SHOP | ES_ALTAR;
    Options.safe_zero_exp          = true;
    Options.target_zero_exp        = true;
    Options.target_wrap            = false;
    Options.target_oos             = true;
    Options.target_los_first       = true;
    Options.dump_kill_places       = KDO_ONE_PLACE;
    Options.dump_message_count     = 4;
    Options.dump_item_origins      = IODS_ARTIFACTS | IODS_RODS;
    Options.dump_item_origin_price = -1;

    Options.drop_mode              = DM_SINGLE;

    Options.flush_input[ FLUSH_ON_FAILURE ]     = true;
    Options.flush_input[ FLUSH_BEFORE_COMMAND ] = false;
    Options.flush_input[ FLUSH_ON_MESSAGE ]     = false;
    Options.flush_input[ FLUSH_LUA ]            = true;

    Options.lowercase_invocations  = false; 

    // Note: These fire options currently match the old behaviour. -- bwr
    Options.fire_items_start       = 0;           // start at slot 'a'

    Options.fire_order[0] = FIRE_LAUNCHER;      // fire first from bow...
    Options.fire_order[1] = FIRE_DART;          // then only consider darts

    // clear the rest of the list
    for (int i = 2; i < NUM_FIRE_TYPES; i++)
        Options.fire_order[i] = FIRE_NONE;

    Options.fsim_rounds = 40000L;
    Options.fsim_mons   = "worm";
    Options.fsim_str = Options.fsim_int = Options.fsim_dex = 15;
    Options.fsim_xl  = 10;
    Options.fsim_kit.clear();

    // These are only used internally, and only from the commandline:
    // XXX: These need a better place.
    Options.sc_entries             = 0;
    Options.sc_format              = SCORE_REGULAR;

//#ifdef USE_COLOUR_OPTS
    Options.friend_brand    = CHATTR_NORMAL;
    Options.heap_brand      = CHATTR_NORMAL;
    Options.stab_brand      = CHATTR_NORMAL;
    Options.may_stab_brand  = CHATTR_NORMAL;
    Options.no_dark_brand = 0;
//#endif

#ifdef WIZARD
    Options.wiz_mode      = WIZ_NO;
#endif

    // map each colour to itself as default
#ifdef USE_8_COLOUR_TERM_MAP
    for (int i = 0; i < 16; i++)
        Options.colour[i] = i % 8;

    Options.colour[ DARKGREY ] = COL_TO_REPLACE_DARKGREY;
#else
    for (int i = 0; i < 16; i++)
        Options.colour[i] = i;
#endif
    
    // map each channel to plain (well, default for now since I'm testing)
    for (int i = 0; i < NUM_MESSAGE_CHANNELS; i++)
        Options.channels[i] = MSGCOL_DEFAULT;

    // Clear vector options.
    Options.dump_order.clear();
    add_dump_fields("header,stats,misc,notes,inventory,skills,"
                   "spells,mutations,messages,screenshot,kills");

    Options.banned_objects.clear();
    Options.note_monsters.clear(); 
    Options.note_messages.clear(); 
    Options.autoinscriptions.clear();
    Options.note_items.clear();
    Options.note_skill_levels.clear();
    Options.travel_stop_message.clear();
    Options.sound_mappings.clear();
    Options.menu_colour_mappings.clear();
    Options.drop_filter.clear();
    Options.map_file_name.clear();
    Options.named_options.clear();

    clear_cset_overrides();
    clear_feature_overrides();

    // Map each category to itself. The user can override in init.txt
    Options.kill_map[KC_YOU] = KC_YOU;
    Options.kill_map[KC_FRIENDLY] = KC_FRIENDLY;
    Options.kill_map[KC_OTHER] = KC_OTHER;

    // Setup travel information. What's a better place to do this?
    initialise_travel();
}

// returns where the init file was read from
std::string read_init_file(bool runscript)
{
    const char* locations_data[][2] = {
        { SysEnv.crawl_rc, "" },
        { SysEnv.crawl_dir, "init.txt" },
#ifdef MULTIUSER
        { SysEnv.home, "/.crawlrc" },
        { SysEnv.home, "init.txt" },
#endif
        { "", "init.txt" },
#ifdef WIN32CONSOLE
        { "", ".crawlrc" },
        { "", "_crawlrc" },
#endif
        { NULL, NULL }                // placeholder to mark end
    };

    reset_options(!runscript);

    if (!runscript)
        you.your_name[0] = 0;

    FILE* f = NULL;
    char name_buff[kPathLen];
    // Check all possibilities for init.txt
    for ( int i = 0; f == NULL && locations_data[i][1] != NULL; ++i ) {
        // Don't look at unset options
        if ( locations_data[i][0] != NULL ) {
            snprintf( name_buff, sizeof name_buff, "%s%s",
                      locations_data[i][0], locations_data[i][1] );
            f = fopen( name_buff, "r" );
        }
    }

    if ( f == NULL )
    {
#ifdef MULTIUSER
        return "not found (~/.crawlrc missing)";
#else
        return "not found (init.txt missing from current directory)";
#endif
    }

    if (!runscript)
        read_startup_prefs();

    read_options(f, runscript);
    fclose(f);
    return std::string(name_buff);
}                               // end read_init_file()

static void read_startup_prefs()
{
    std::string fn = get_prefs_filename();
    FILE *f = fopen(fn.c_str(), "r");
    if (!f)
        return;
    read_options(f);
    fclose(f);

    Options.prev_randpick = Options.random_pick;
    Options.prev_weapon   = Options.weapon;
    Options.prev_pr       = Options.priest;
    Options.prev_dk       = Options.death_knight;
    Options.prev_ck       = Options.chaos_knight;
    Options.prev_cls      = Options.cls;
    Options.prev_race     = Options.race;
    Options.prev_book     = Options.book;
    Options.prev_name     = you.your_name;

    reset_startup_options();
}

static void write_newgame_options(FILE *f)
{
    // Write current player name
    fprintf(f, "name = %s\n", you.your_name);

    if (Options.prev_randpick)
        Options.prev_race = Options.prev_cls = '?';

    // Race selection
    if (Options.prev_race)
        fprintf(f, "race = %c\n", Options.prev_race);
    if (Options.prev_cls)
        fprintf(f, "class = %c\n", Options.prev_cls);

    if (Options.prev_weapon != WPN_UNKNOWN)
        fprintf(f, "weapon = %s\n", weapon_to_str(Options.prev_weapon).c_str());

    if (Options.prev_ck != GOD_NO_GOD)
    {
        fprintf(f, "chaos_knight = %s\n",
                Options.prev_ck == GOD_XOM? "xom" :
                Options.prev_ck == GOD_MAKHLEB? "makhleb" :
                                            "random");
    }
    if (Options.prev_dk != DK_NO_SELECTION)
    {
        fprintf(f, "death_knight = %s\n",
                Options.prev_dk == DK_NECROMANCY? "necromancy" :
                Options.prev_dk == DK_YREDELEMNUL? "yredelemnul" :
                                            "random");
    }
    if (Options.prev_pr != GOD_NO_GOD)
    {
        fprintf(f, "priest = %s\n",
                Options.prev_pr == GOD_ZIN? "zin" :
                Options.prev_pr == GOD_YREDELEMNUL? "yredelemnul" :
                                            "random");
    }

    if (Options.prev_book != SBT_NO_SELECTION )
    {
	fprintf(f, "book = %s\n",
		Options.prev_book == SBT_FIRE ? "fire" :
		Options.prev_book == SBT_COLD ? "cold" :
		Options.prev_book == SBT_SUMM ? "summ" :
		"random");
    }
}

void write_newgame_options_file()
{
    std::string fn = get_prefs_filename();
    FILE *f = fopen(fn.c_str(), "w");
    if (!f)
        return;
    write_newgame_options(f);
    fclose(f);
}

void save_player_name()
{
    if (!Options.remember_name)
        return ;

    std::string playername = you.your_name;

    // Read other preferences
    read_startup_prefs();

    // Put back your name
    strncpy(you.your_name, playername.c_str(), kNameLen);
    you.your_name[kNameLen - 1] = 0;

    // And save
    write_newgame_options_file();
}
 
void read_options(FILE *f, bool runscript)
{
    FileLineInput fl(f);
    read_options(fl, runscript);
}

void read_options(const std::string &s, bool runscript)
{
    StringLineInput st(s);
    read_options(st, runscript);
}

static void read_options(InitLineInput &il, bool runscript)
{
    unsigned int line = 0;
    
    bool inscriptblock = false;
    bool inscriptcond  = false;
    bool isconditional = false;

    bool l_init        = false;

    std::string luacond;
    std::string luacode;
    while (!il.eof())
    {
        std::string s   = il.getline();
        std::string str = s;
        line++;

        trim_string( str );

        // This is to make some efficient comments
        if ((str.empty() || str[0] == '#') && !inscriptcond && !inscriptblock)
            continue;

        if (!inscriptcond && (str.find("L<") == 0 || str.find("<") == 0))
        {
            // The init file is now forced into isconditional mode.
            isconditional = true;
            inscriptcond  = true;

            str = str.substr( str.find("L<") == 0? 2 : 1 );
            // Is this a one-liner?
            if (!str.empty() && str[ str.length() - 1 ] == '>') {
                inscriptcond = false;
                str = str.substr(0, str.length() - 1);
            }

            if (!str.empty() && runscript)
            {
                // If we're in the middle of an option block, close it.
                if (luacond.length() && l_init)
                {
                    luacond += "]] )\n";
                    l_init = false;
                }
                luacond += str + "\n";
            }
            continue;
        }
        else if (inscriptcond && 
                (str.find(">") == str.length() - 1 || str == ">L"))
        {
            inscriptcond = false;
            str = str.substr(0, str.length() - 1);
            if (!str.empty() && runscript)
                luacond += str + "\n";
            continue;
        }
        else if (inscriptcond)
        {
            if (runscript)
                luacond += s + "\n";
            continue;
        }

        // Handle blocks of Lua
        if (!inscriptblock && (str.find("Lua{") == 0 || str.find("{") == 0))
        {
            inscriptblock = true;
            luacode.clear();

            // Strip leading Lua[
            str = str.substr( str.find("Lua{") == 0? 4 : 1 );

            if (!str.empty() && str.find("}") == str.length() - 1)
            {
                str = str.substr(0, str.length() - 1);
                inscriptblock = false;
            }

            if (!str.empty())
                luacode += str + "\n";

            if (!inscriptblock && runscript)
            {
#ifdef CLUA_BINDINGS
                clua.execstring(luacode.c_str());
                if (clua.error.length())
                    fprintf(stderr, "Lua error: %s\n", clua.error.c_str());
                luacode.clear();
#endif
            }
            
            continue;
        }
        else if (inscriptblock && (str == "}Lua" || str == "}"))
        {
            inscriptblock = false;
#ifdef CLUA_BINDINGS
            if (runscript)
            {
                clua.execstring(luacode.c_str());
                if (clua.error.length())
                    fprintf(stderr, "Lua error: %s\n",
                            clua.error.c_str());
            }
#endif
            luacode.clear();
            continue;
        }
        else if (inscriptblock)
        {
            luacode += s + "\n";
            continue;
        }

        if (isconditional && runscript)
        {
            if (!l_init)
            {
                luacond += "crawl.setopt( [[\n";
                l_init = true;
            }

            luacond += s + "\n";
            continue;
        }

        parse_option_line(str, runscript);
    }

#ifdef CLUA_BINDINGS
    if (runscript && !luacond.empty())
    {
        if (l_init)
            luacond += "]] )\n";
        clua.execstring(luacond.c_str());
        if (clua.error.length())
        {
            mpr( ("Lua error: " + clua.error).c_str() );
        }
    }
#endif
}

static int str_to_killcategory(const std::string &s)
{
   static const char *kc[] = {
       "you",
       "friend",
       "other",
   };

   for (unsigned i = 0; i < sizeof(kc) / sizeof(*kc); ++i) {
       if (s == kc[i])
           return i;
   }
   return -1;
}

static void do_kill_map(const std::string &from, const std::string &to)
{
    int ifrom = str_to_killcategory(from),
        ito   = str_to_killcategory(to);
    if (ifrom != -1 && ito != -1)
        Options.kill_map[ifrom] = ito;
}

void parse_option_line(const std::string &str, bool runscript)
{
    std::string key = "";
    std::string subkey = "";
    std::string field = "";

    int first_equals = str.find('=');
    int first_dot = str.find('.');

    bool plus_equal = false;

    // all lines with no equal-signs we ignore
    if (first_equals < 0)
        return;

    if (first_dot > 0 && first_dot < first_equals)
    {
        key    = str.substr( 0, first_dot );
        subkey = str.substr( first_dot + 1, first_equals - first_dot - 1 );
        field  = str.substr( first_equals + 1 );
    }
    else
    {
        // no subkey (dots are okay in value field)
        key    = str.substr( 0, first_equals );
        subkey = "";
        field  = str.substr( first_equals + 1 );
    }

    // Clean up our data...
    tolower_string( trim_string( key ) );

    // Is this a case of key += val?
    if (key.length() && key[key.length() - 1] == '+')
    {
        plus_equal = true;
        key = key.substr(0, key.length() - 1);
        trim_string(key);
    }

    tolower_string( trim_string( subkey ) );

    // some fields want capitals... none care about external spaces
    trim_string( field );

    // Keep unlowercased field around
    std::string orig_field = field;

    if (key != "name" && key != "crawl_dir" 
        && key != "race" && key != "class" && key != "ban_pickup"
        && key != "stop_travel" && key != "sound" 
        && key != "travel_stop_message"
        && key != "drop_filter" && key != "lua_file"
	&& key != "note_items" && key != "autoinscribe"
	&& key != "note_monsters" && key != "note_messages"
        && key.find("cset") != 0 && key != "dungeon"
        && key != "feature")
    {
        tolower_string( field );
    }

    // everything not a valid line is treated as a comment
    if (key == "autopickup")
    {
        // clear out autopickup
        Options.autopickups = 0L;

        for (size_t i = 0; i < field.length(); i++)
        {
            char type = field[i];

            // Make the amulet symbol equiv to ring -- bwross
            switch (type)
            {
            case '"':
                // also represents jewellery
                type = '=';
                break;

            case '|':
                // also represents staves
                type = '\\';
                break;

            case ':':
                // also represents books
                type = '+';
                break;

            case 'x':
                // also corpses
                type = 'X';
                break;
            }

            int j;
            for (j = 0; j < obj_syms_len && type != obj_syms[j]; j++)
                ;

            if (j < obj_syms_len)
                Options.autopickups |= (1L << j);
            else
            {
                fprintf( stderr, "Bad object type '%c' for autopickup.\n",
                         type );
            }
        }
    }
    else if (key == "name")
    {
        // field is already cleaned up from trim_string()
        strncpy(you.your_name, field.c_str(), kNameLen);
        you.your_name[ kNameLen - 1 ] = 0;
    }
    else if (key == "verbose_dump")
    {
        // gives verbose info in char dumps
        Options.verbose_dump = read_bool( field, Options.verbose_dump );
    }
    else if (key == "char_set" || key == "ascii_display")
    {
        bool valid = true;

        if (key == "ascii_display")
        {
            Options.char_set = 
                read_bool(field, Options.char_set == CSET_ASCII)?
                    CSET_ASCII
                  : CSET_IBM;
            valid = true;
        }
        else
        {
            if (field == "ascii")
                Options.char_set = CSET_ASCII;
            else if (field == "ibm")
                Options.char_set = CSET_IBM;
            else if (field == "dec")
                Options.char_set = CSET_DEC;
            else 
            {
                fprintf( stderr, "Bad character set: %s\n", field.c_str() );
                valid = false;
            }
        }
        if (valid)
            init_char_table(Options.char_set);
    }
    else if (key == "default_autopickup")
    {
	// should autopickup default to on or off?
	autopickup_on = read_bool( field, autopickup_on );
    }
    else if (key == "default_autoprayer")
    {
	// should autoprayer default to on or off?
	autoprayer_on = read_bool( field, autoprayer_on );
    }
    else if (key == "default_fizzlecheck")
    {
	// should fizzlecheck default to on or off?
	fizzlecheck_on = read_bool( field, fizzlecheck_on );
    }
    else if (key == "detailed_stat_dump")
    {
        Options.detailed_stat_dump = 
            read_bool( field, Options.detailed_stat_dump );
    }
    else if (key == "clean_map")
    {
        // removes monsters/clouds from map
        Options.clean_map = read_bool( field, Options.clean_map );
    }
    else if (key == "colour_map" || key == "color_map")
    {
        // colour-codes play-screen map
        Options.colour_map = read_bool( field, Options.colour_map );
    }
    else if (key == "easy_confirm")
    {
        // allows both 'Y'/'N' and 'y'/'n' on yesno() prompts
        if (field == "none")
            Options.easy_confirm = CONFIRM_NONE_EASY;
        else if (field == "safe")
            Options.easy_confirm = CONFIRM_SAFE_EASY;
        else if (field == "all")
            Options.easy_confirm = CONFIRM_ALL_EASY;
    }
    else if (key == "easy_quit_item_lists")
    {
        // allow aborting of item lists with space
        Options.easy_quit_item_prompts = read_bool( field, 
                                        Options.easy_quit_item_prompts );
    }
    else if (key == "easy_open")
    {
        // automatic door opening with movement
        Options.easy_open = read_bool( field, Options.easy_open );
    }
    else if (key == "easy_armor" 
            || key == "easy_armour" 
            || key == "easy_unequip")
    {
        // automatic removal of armour when dropping
        Options.easy_unequip = read_bool( field, Options.easy_unequip );
    }
    else if (key == "easy_butcher")
    {
        // automatic knife switching
        Options.easy_butcher = read_bool( field, Options.easy_butcher );
    }
    else if (key == "lua_file" && runscript)
    {
#ifdef CLUA_BINDINGS
        clua.execfile(field.c_str());
        if (clua.error.length())
            fprintf(stderr, "Lua error: %s\n",
                    clua.error.c_str());
#endif
    }
    else if (key == "colour" || key == "color")
    {
        const int orig_col   = str_to_colour( subkey );
        const int result_col = str_to_colour( field );

        if (orig_col != -1 && result_col != -1)
            Options.colour[orig_col] = result_col;
        else
        {
            fprintf( stderr, "Bad colour -- %s=%d or %s=%d\n",
                     subkey.c_str(), orig_col, field.c_str(), result_col );
        }
    }
    else if (key == "channel")
    {
        const int chnl = str_to_channel( subkey );
        const int col  = str_to_channel_colour( field );

        if (chnl != -1 && col != -1)
            Options.channels[chnl] = col;
        else if (chnl == -1)
            fprintf( stderr, "Bad channel -- %s\n", subkey.c_str() );
        else if (col == -1)
            fprintf( stderr, "Bad colour -- %s\n", field.c_str() );
    }
    else if (key == "background")
    {
        // change background colour
        // Experimental! This may look really bad!
        const int col = str_to_colour( field );

        if (col != -1)
            Options.background = col;
        else
            fprintf( stderr, "Bad colour -- %s\n", field.c_str() );

    }
    else if (key == "detected_item_colour")
    {
        const int col = str_to_colour( field );
        if (col != -1)
            Options.detected_item_colour = col;
        else
            fprintf( stderr, "Bad detected_item_colour -- %s\n",
                     field.c_str());
    }
    else if (key == "detected_monster_colour")
    {
        const int col = str_to_colour( field );
        if (col != -1)
            Options.detected_monster_colour = col;
        else
            fprintf( stderr, "Bad detected_monster_colour -- %s\n",
                     field.c_str());
    }
    else if (key == "remembered_monster_colour")
    {
        if (field == "real")
            Options.remembered_monster_colour = 0xFFFFU;
        else if (field == "auto")
            Options.remembered_monster_colour = 0;
        else {
            const int col = str_to_colour( field );
            if (col != -1)
                Options.remembered_monster_colour = col;
            else
                fprintf( stderr, "Bad remembered_monster_colour -- %s\n",
                         field.c_str());
        }
    }
    else if (key.find(interrupt_prefix) == 0)
    {
        set_activity_interrupt(key.substr(interrupt_prefix.length()), 
                               field,
                               plus_equal);
    }
    else if (key.find("cset") == 0)
    {
        std::string cset = key.substr(4);
        if (cset[0] == '_')
            cset = cset.substr(1);

        char_set_type cs = NUM_CSET;
        if (cset == "ascii")
            cs = CSET_ASCII;
        else if (cset == "ibm")
            cs = CSET_IBM;
        else if (cset == "dec")
            cs = CSET_DEC;

        add_cset_override(cs, field);
    }
    else if (key == "feature" || key == "dungeon")
    {
        std::vector<std::string> defs = split_string(";", field);
        
        for (int i = 0, size = defs.size(); i < size; ++i)
            add_feature_override( defs[i] );
    }
    else if (key == "friend_brand")
    {
        // Use curses attributes to mark friend
        // Some may look bad on some terminals.
        // As a suggestion, try "rxvt -rv -fn 10x20" under Un*xes
        Options.friend_brand = curses_attribute(field);
    }
    else if (key == "stab_brand")
    {
        Options.stab_brand = curses_attribute(field);
    }
    else if (key == "may_stab_brand")
    {
        Options.may_stab_brand = curses_attribute(field);
    }
    else if (key == "no_dark_brand")
    {
        // This is useful for terms where dark grey does
        // not have standout modes (since it's black on black).
        // This option will use light-grey instead in these cases.
        Options.no_dark_brand = read_bool( field, Options.no_dark_brand );
    }
    else if (key == "heap_brand")
    {
        // See friend_brand option upstairs. no_dark_brand applies
        // here as well.
        Options.heap_brand = curses_attribute(field);
    }
    else if (key == "show_uncursed")
    {
        // label known uncursed items as "uncursed"
        Options.show_uncursed = read_bool( field, Options.show_uncursed );
    }
    else if (key == "always_greet")
    {
        // show greeting when reloading game
        Options.always_greet = read_bool( field, Options.always_greet );
    }
    else if (key == "weapon")
    {
        // choose this weapon for classes that get choice
        Options.weapon = str_to_weapon( field );
    }
    else if (key == "book")
    {
	// choose this book for classes that get choice
	Options.book = str_to_book( field );
    }
    else if (key == "chaos_knight")
    {
        // choose god for Chaos Knights
        if (field == "xom")
            Options.chaos_knight = GOD_XOM;
        else if (field == "makhleb")
            Options.chaos_knight = GOD_MAKHLEB;
        else if (field == "random")
            Options.chaos_knight = GOD_RANDOM;
    }
    else if (key == "death_knight")
    {
        if (field == "necromancy")
            Options.death_knight = DK_NECROMANCY;
        else if (field == "yredelemnul")
            Options.death_knight = DK_YREDELEMNUL;
        else if (field == "random")
            Options.death_knight = DK_RANDOM;
    }
    else if (key == "priest")
    {
        // choose this weapon for classes that get choice
        if (field == "zin")
            Options.priest = GOD_ZIN;
        else if (field == "yredelemnul")
            Options.priest = GOD_YREDELEMNUL;
        else if (field == "random")
            Options.priest = GOD_RANDOM;
    }
    else if (key == "fire_items_start")
    {
        if (isalpha( field[0] ))
            Options.fire_items_start = letter_to_index( field[0] ); 
        else
        {
            fprintf( stderr, "Bad fire item start index -- %s\n",
                     field.c_str() );
        }
    }
    else if (key == "assign_item_slot")
    {
        if (field == "forward")
            Options.assign_item_slot = SS_FORWARD;
        else if (field == "backward")
            Options.assign_item_slot = SS_BACKWARD;
    }
    else if (key == "fire_order")
    {
        str_to_fire_order( field, Options.fire_order );
    }
    else if (key == "random_pick")
    {
        // randomly generate character
        Options.random_pick = read_bool( field, Options.random_pick );
    }
    else if (key == "remember_name")
    {
        Options.remember_name = read_bool( field, Options.remember_name );
    }   
    else if (key == "hp_warning")
    {
        Options.hp_warning = atoi( field.c_str() );
        if (Options.hp_warning < 0 || Options.hp_warning > 100)
        {
            Options.hp_warning = 0;
            fprintf( stderr, "Bad HP warning percentage -- %s\n",
                     field.c_str() );
        }
    }
    else if (key == "ood_interesting")
    {
	Options.ood_interesting = atoi( field.c_str() );
    }
    else if (key == "note_monsters")
    {
        append_vector(Options.note_monsters, split_string(",", field));
    }
    else if (key == "note_messages")
    {
        append_vector(Options.note_messages, split_string(",", field));
    }
    else if (key == "note_hp_percent")
    {
	Options.note_hp_percent = atoi( field.c_str() );
        if (Options.note_hp_percent < 0 || Options.note_hp_percent > 100)
        {
            Options.note_hp_percent = 0;
            fprintf( stderr, "Bad HP note percentage -- %s\n",
                     field.c_str() );
        }
    }
    else if (key == "hp_attention")
    {
        Options.hp_attention = atoi( field.c_str() );
        if (Options.hp_attention < 0 || Options.hp_attention > 100)
        {
            Options.hp_attention = 0;
            fprintf( stderr, "Bad HP attention percentage -- %s\n",
                     field.c_str() );
        }
    }
    else if (key == "crawl_dir")
    {
        // We shouldn't bother to allocate this a second time
        // if the user puts two crawl_dir lines in the init file.
        if (!SysEnv.crawl_dir)
            SysEnv.crawl_dir = (char *) calloc(kPathLen, sizeof(char));

        if (SysEnv.crawl_dir)
        {
            strncpy(SysEnv.crawl_dir, field.c_str(), kNameLen - 1);
            SysEnv.crawl_dir[ kNameLen - 1 ] = 0;
        }
    }
    else if (key == "race")
    {
        Options.race = str_to_race( field );

        if (Options.race == 0)
            fprintf( stderr, "Unknown race choice: %s\n", field.c_str() );
    }
    else if (key == "class")
    {
        Options.cls = str_to_class( field );

        if (Options.cls == 0)
            fprintf( stderr, "Unknown class choice: %s\n", field.c_str() );
    }
    else if (key == "auto_list")
    {
        Options.auto_list = read_bool( field, Options.auto_list );
    }
    else if (key == "confirm_self_target")
    {
	Options.confirm_self_target = read_bool( field, Options.confirm_self_target );
    }
    else if (key == "safe_autopickup")
    {
	Options.safe_autopickup = read_bool( field, Options.safe_autopickup );
    }
    else if (key == "use_notes")
    {
        Options.use_notes = read_bool( field, Options.use_notes );
    }
    else if (key == "note_skill_max")
    {
        Options.note_skill_max = read_bool( field, Options.note_skill_max );
    }
    else if (key == "note_all_spells")
    {
        Options.note_all_spells = read_bool( field, Options.note_all_spells );
    }
    else if (key == "delay_message_clear")
    {
        Options.delay_message_clear = read_bool( field, Options.delay_message_clear );
    }
    else if (key == "terse_hand")
    {
        Options.terse_hand = read_bool( field, Options.terse_hand );
    }
    else if (key == "increasing_skill_progress")
    {
        Options.increasing_skill_progress = read_bool( field, Options.increasing_skill_progress );
    }
    else if (key == "flush")
    {
        if (subkey == "failure")
        {
            Options.flush_input[FLUSH_ON_FAILURE] 
                = read_bool(field, Options.flush_input[FLUSH_ON_FAILURE]);
        }
        else if (subkey == "command")
        {
            Options.flush_input[FLUSH_BEFORE_COMMAND] 
                = read_bool(field, Options.flush_input[FLUSH_BEFORE_COMMAND]);
        }
        else if (subkey == "message")
        {
            Options.flush_input[FLUSH_ON_MESSAGE] 
                = read_bool(field, Options.flush_input[FLUSH_ON_MESSAGE]);
        }
        else if (subkey == "lua")
        {
            Options.flush_input[FLUSH_LUA] 
                = read_bool(field, Options.flush_input[FLUSH_LUA]);
        }
    }
    else if (key == "lowercase_invocations")
    {
        Options.lowercase_invocations 
                = read_bool(field, Options.lowercase_invocations);
    }
    else if (key == "wiz_mode")
    {
        // wiz_mode is recognized as a legal key in all compiles -- bwr
#ifdef WIZARD
        if (field == "never")
            Options.wiz_mode = WIZ_NEVER;
        else if (field == "no")
            Options.wiz_mode = WIZ_NO;
        else if (field == "yes")
            Options.wiz_mode = WIZ_YES;
        else
            fprintf(stderr, "Unknown wiz_mode option: %s\n", field.c_str());
#endif
    }
    else if (key == "ban_pickup")
    {
        append_vector(Options.banned_objects, split_string(",", field));
    }
    else if (key == "note_items")
    {
	append_vector(Options.note_items, split_string(",", field));
    }
    else if (key == "autoinscribe")
    {
	std::vector<std::string> thesplit =
	    split_string(":", field);
	Options.autoinscriptions.push_back(
	    std::pair<text_pattern,std::string>(thesplit[0],
						thesplit[1]));
    }
    else if (key == "map_file_name")
    {
	Options.map_file_name = field;
    }
    else if (key == "note_skill_levels")
    {
	std::vector<std::string> thesplit = split_string(",", field);
	for ( unsigned i = 0; i < thesplit.size(); ++i ) {
	    int num = atoi(thesplit[i].c_str());
	    if ( num > 0 && num <= 27 ) {
		Options.note_skill_levels.push_back(num);
	    }
	    else {
		fprintf(stderr, "Bad skill level to note -- %s\n",
			thesplit[i].c_str());
		continue;
	    }
	}
    }
    else if (key == "pickup_thrown")
    {
        Options.pickup_thrown = read_bool(field, Options.pickup_thrown);
    }
    else if (key == "pickup_dropped")
    {
        Options.pickup_dropped = read_bool(field, Options.pickup_dropped);
    }
    else if (key == "show_waypoints")
    {
        Options.show_waypoints = read_bool(field, Options.show_waypoints);
    }
    else if (key == "fsim_kit")
    {
        append_vector(Options.fsim_kit, split_string(",", field));
    }
    else if (key == "fsim_rounds")
    {
        Options.fsim_rounds = atol(field.c_str());
        if (Options.fsim_rounds < 1000)
            Options.fsim_rounds = 1000;
        if (Options.fsim_rounds > 500000L)
            Options.fsim_rounds = 500000L;
    }
    else if (key == "fsim_mons")
    {
        Options.fsim_mons = field;
    }
    else if (key == "fsim_str")
    {
        Options.fsim_str = atoi(field.c_str());
    }
    else if (key == "fsim_int")
    {
        Options.fsim_int = atoi(field.c_str());
    }
    else if (key == "fsim_dex")
    {
        Options.fsim_dex = atoi(field.c_str());
    }
    else if (key == "fsim_xl")
    {
        Options.fsim_xl = atoi(field.c_str());
    }
    else if (key == "sort_menus")
    {
        Options.sort_menus = read_bool(field, Options.sort_menus);
    }
    else if (key == "travel_delay")
    {
        // Read travel delay in milliseconds.
        Options.travel_delay = atoi( field.c_str() );
        if (Options.travel_delay < -1)
            Options.travel_delay = -1;
        if (Options.travel_delay > 2000)
            Options.travel_delay = 2000;
    }
    else if (key == "level_map_cursor_step")
    {
        Options.level_map_cursor_step = atoi( field.c_str() );
        if (Options.level_map_cursor_step < 1)
            Options.level_map_cursor_step = 1;
        if (Options.level_map_cursor_step > 50)
            Options.level_map_cursor_step = 50;
    }
    else if (key == "macro_meta_entry")
    {
        Options.macro_meta_entry = read_bool(field, Options.macro_meta_entry);
    }
    else if (key == "travel_stair_cost")
    {
        Options.travel_stair_cost = atoi( field.c_str() );
        if (Options.travel_stair_cost < 1)
            Options.travel_stair_cost = 1;
        else if (Options.travel_stair_cost > 1000)
            Options.travel_stair_cost = 1000;
    }
    else if (key == "travel_exclude_radius2")
    {
        Options.travel_exclude_radius2 = atoi( field.c_str() );
        if (Options.travel_exclude_radius2 < 0)
            Options.travel_exclude_radius2 = 0;
        else if (Options.travel_exclude_radius2 > 400)
            Options.travel_exclude_radius2 = 400;
    }
    else if (key == "stop_travel" || key == "travel_stop_message")
    {
        std::vector<std::string> fragments = split_string(",", field);
        for (int i = 0, count = fragments.size(); i < count; ++i)
        {
            if (fragments[i].length() == 0)
                continue;

            std::string::size_type pos = fragments[i].find(":");
            if (pos && pos != std::string::npos)
            {
                std::string prefix = fragments[i].substr(0, pos);
                int channel = str_to_channel( prefix );
                if (channel != -1 || prefix == "any")
                {
                    std::string s = fragments[i].substr( pos + 1 );
                    trim_string( s );
                    Options.travel_stop_message.push_back(
                       message_filter( channel, s ) );
                    continue;
                }
            }

            Options.travel_stop_message.push_back(
                    message_filter( fragments[i] ) );
        }
    }
    else if (key == "drop_filter")
    {
        append_vector(Options.drop_filter, split_string(",", field));
    }
    else if (key == "travel_avoid_terrain")
    {
        std::vector<std::string> seg = split_string(",", field);
        for (int i = 0, count = seg.size(); i < count; ++i)
            prevent_travel_to( seg[i] );
    }
    else if (key == "travel_colour")
    {
        Options.travel_colour = read_bool(field, Options.travel_colour);
    }
    else if (key == "tc_reachable")
    {
        Options.tc_reachable = str_to_colour(field, Options.tc_reachable);
    }
    else if (key == "tc_excluded")
    {
        Options.tc_excluded = str_to_colour(field, Options.tc_excluded);
    }
    else if (key == "tc_exclude_circle")
    {
        Options.tc_exclude_circle =
            str_to_colour(field, Options.tc_exclude_circle);
    }
    else if (key == "tc_dangerous")
    {
        Options.tc_dangerous = str_to_colour(field, Options.tc_dangerous);
    }
    else if (key == "tc_disconnected")
    {
        Options.tc_disconnected = str_to_colour(field, Options.tc_disconnected);
    }
    else if (key == "item_colour")
    {
        Options.item_colour = read_bool(field, Options.item_colour);
    }
    else if (key == "easy_exit_menu")
    {
        Options.easy_exit_menu = read_bool(field, Options.easy_exit_menu);
    }
    else if (key == "dos_use_background_intensity")
    {
        Options.dos_use_background_intensity = 
            read_bool(field, Options.dos_use_background_intensity);
    }
    else if (key == "explore_stop")
    {
        Options.explore_stop = ES_NONE;
        std::vector<std::string> stops = split_string(",", field);
        for (int i = 0, count = stops.size(); i < count; ++i)
        {
            const std::string &c = stops[i];
            if (c == "item" || c == "items")
                Options.explore_stop |= ES_ITEM;
            else if (c == "shop" || c == "shops")
                Options.explore_stop |= ES_SHOP;
            else if (c == "stair" || c == "stairs")
                Options.explore_stop |= ES_STAIR;
            else if (c == "altar" || c == "altars")
                Options.explore_stop |= ES_ALTAR;
        }
    }
#ifdef STASH_TRACKING
    else if (key == "stash_tracking")
    {
        Options.stash_tracking =
             field == "dropped" ? STM_DROPPED  :
             field == "all"     ? STM_ALL      :
                                  STM_EXPLICIT;
    }
    else if (key == "stash_filter")
    {
        std::vector<std::string> seg = split_string(",", field);
        for (int i = 0, count = seg.size(); i < count; ++i)
            Stash::filter( seg[i] );
    }
#endif
    else if (key == "sound")
    {
        std::vector<std::string> seg = split_string(",", field);
        for (int i = 0, count = seg.size(); i < count; ++i) {
            const std::string &sub = seg[i];
            std::string::size_type cpos = sub.find(":", 0);
            if (cpos != std::string::npos) {
                sound_mapping mapping;
                mapping.pattern = sub.substr(0, cpos);
                mapping.soundfile = sub.substr(cpos + 1);
                Options.sound_mappings.push_back(mapping);
            }
        }
    }
    else if (key == "menu_colour" || key == "menu_color")
    {
        std::vector<std::string> seg = split_string(",", field);
        for (int i = 0, count = seg.size(); i < count; ++i) {
            const std::string &sub = seg[i];
            std::string::size_type cpos = sub.find(":", 0);
            if (cpos != std::string::npos) {
                colour_mapping mapping;
                mapping.pattern = sub.substr(cpos + 1);
                mapping.colour  = str_to_colour(sub.substr(0, cpos));
                if (mapping.colour != -1)
                    Options.menu_colour_mappings.push_back(mapping);
            }
        }
    }
    else if (key == "dump_order")
    {
        if (!plus_equal)
            Options.dump_order.clear();

        add_dump_fields(field);
    }
    else if (key == "dump_kill_places") 
    {
        Options.dump_kill_places =
            field == "none"? KDO_NO_PLACES :
            field == "all" ? KDO_ALL_PLACES :
                             KDO_ONE_PLACE;
    }
    else if (key == "kill_map")
    {
        std::vector<std::string> seg = split_string(",", field);
        for (int i = 0, count = seg.size(); i < count; ++i)
        {
            const std::string &s = seg[i];
            std::string::size_type cpos = s.find(":", 0);
            if (cpos != std::string::npos) {
                std::string from = s.substr(0, cpos);
                std::string to   = s.substr(cpos + 1);
                do_kill_map(from, to);
            }
        }
    }
    else if (key == "dump_message_count")
    {
        // Capping is implicit
        Options.dump_message_count = atoi( field.c_str() );
    }
    else if (key == "dump_item_origins")
    {
        Options.dump_item_origins = IODS_PRICE;
        std::vector<std::string> choices = split_string(",", field);
        for (int i = 0, count = choices.size(); i < count; ++i)
        {
            const std::string &ch = choices[i];
            if (ch == "artifacts")
                Options.dump_item_origins |= IODS_ARTIFACTS;
            else if (ch == "ego_arm" || ch == "ego armour" 
                    || ch == "ego_armour")
                Options.dump_item_origins |= IODS_EGO_ARMOUR;
            else if (ch == "ego_weap" || ch == "ego weapon" 
                    || ch == "ego_weapon" || ch == "ego weapons"
                    || ch == "ego_weapons")
                Options.dump_item_origins |= IODS_EGO_WEAPON;
            else if (ch == "jewellery" || ch == "jewelry")
                Options.dump_item_origins |= IODS_JEWELLERY;
            else if (ch == "runes")
                Options.dump_item_origins |= IODS_RUNES;
            else if (ch == "rods")
                Options.dump_item_origins |= IODS_RODS;
            else if (ch == "staves")
                Options.dump_item_origins |= IODS_STAVES;
            else if (ch == "books")
                Options.dump_item_origins |= IODS_BOOKS;
            else if (ch == "all" || ch == "everything")
                Options.dump_item_origins = IODS_EVERYTHING;
        }
    }
    else if (key == "dump_item_origin_price")
    {
        Options.dump_item_origin_price = atoi( field.c_str() );
        if (Options.dump_item_origin_price < -1)
            Options.dump_item_origin_price = -1;
    }
    else if (key == "safe_zero_exp")
    {
	Options.safe_zero_exp = read_bool(field, Options.safe_zero_exp);
    }
    else if (key == "target_zero_exp")
    {
        Options.target_zero_exp = read_bool(field, Options.target_zero_exp);
    }
    else if (key == "target_wrap")
    {
        Options.target_wrap = read_bool(field, Options.target_wrap);
    }
    else if (key == "target_oos")
    {
        Options.target_oos = read_bool(field, Options.target_oos);
    }
    else if (key == "target_los_first")
    {
        Options.target_los_first = read_bool(field, Options.target_los_first);
    }
    else if (key == "drop_mode")
    {
        if (field.find("multi") != std::string::npos)
            Options.drop_mode = DM_MULTI;
        else
            Options.drop_mode = DM_SINGLE;
    }
    // Catch-all else, copies option into map
    else
    {
#ifdef CLUA_BINDINGS
        if (!clua.callbooleanfn(false, "c_process_lua_option", "ss", 
                        key.c_str(), orig_field.c_str()))
#endif
        {
#ifdef CLUA_BINDINGS
            if (clua.error.length())
                mpr(clua.error.c_str());
#endif
            Options.named_options[key] = orig_field;
        }
    }
}

void get_system_environment(void)
{
    // The player's name
    SysEnv.crawl_name = getenv("CRAWL_NAME");

    // The player's pizza
    SysEnv.crawl_pizza = getenv("CRAWL_PIZZA");

    // The directory which contians init.txt, macro.txt, morgue.txt
    // This should end with the appropriate path delimiter.
    SysEnv.crawl_dir = getenv("CRAWL_DIR");

    // The full path to the init file -- this over-rides CRAWL_DIR
    SysEnv.crawl_rc = getenv("CRAWL_RC");

    // rename giant and giant spiked clubs
    SysEnv.board_with_nail = (getenv("BOARD_WITH_NAIL") != NULL);

#ifdef MULTIUSER
    // The user's home directory (used to look for ~/.crawlrc file)
    SysEnv.home = getenv("HOME");
#endif
}                               // end get_system_environment()


// parse args, filling in Options and game environment as we go.
// returns true if no unknown or malformed arguments were found.

static const char *cmd_ops[] = { "scores", "name", "race", "class",
                                 "pizza", "plain", "dir", "rc", "tscores",
                                 "vscores" };

const int num_cmd_ops = 10;
bool arg_seen[num_cmd_ops];

bool parse_args( int argc, char **argv, bool rc_only )
{
    if (argc < 2)           // no args!
        return (true);

    char *arg, *next_arg;
    int current = 1;
    bool nextUsed = false;
    int ecount;

    // initialize
    for(int i=0; i<num_cmd_ops; i++)
        arg_seen[i] = false;

    while(current < argc)
    {
        // get argument
        arg = argv[current];

        // next argument (if there is one)
        if (current+1 < argc)
            next_arg = argv[current+1];
        else
            next_arg = NULL;

        nextUsed = false;

        // arg MUST begin with '-' or '/'
        char c = arg[0];
        if (c != '-' && c != '/')
            return (false);

        // look for match (now we also except --scores)
        if (arg[1] == '-')
            arg = &arg[2];
        else
            arg = &arg[1];

        int o;
        for(o = 0; o < num_cmd_ops; o++)
        {
            if (stricmp(cmd_ops[o], arg) == 0)
                break;
        }

        if (o == num_cmd_ops)
            return (false);

        // disallow options specified more than once.
        if (arg_seen[o] == true)
            return (false);

        // set arg to 'seen'
        arg_seen[o] = true;

        // partially parse next argument
        bool next_is_param = false;
        if (next_arg != NULL)
        {
            if (next_arg[0] != '-' && next_arg[0] != '/')
                next_is_param = true;
        }

        //.take action according to the cmd chosen
        switch(o)
        {
        case 0:             // scores
        case 8:             // tscores
        case 9:             // vscores
            if (!next_is_param)      
                ecount = SCORE_FILE_ENTRIES;            // default
            else // optional number given
            {
                ecount = atoi(next_arg);
                if (ecount < 1)
                    ecount = 1;

                if (ecount > SCORE_FILE_ENTRIES)
                    ecount = SCORE_FILE_ENTRIES;

                nextUsed = true;
            }

            if (!rc_only)
            {
                Options.sc_entries = ecount;

                if (o == 8)
                    Options.sc_format = SCORE_TERSE;
                else if (o == 9)
                    Options.sc_format = SCORE_VERBOSE;

            }
            break;

        case 1:             // name
            if (!next_is_param)
                return (false);

            if (!rc_only)
            {
                strncpy(you.your_name, next_arg, kNameLen);
                you.your_name[ kNameLen - 1 ] = 0;
            }

            nextUsed = true;
            break;

        case 2:             // race
        case 3:             // class
            if (!next_is_param)
                return (false);

            // if (strlen(next_arg) != 1)
            //    return (false);

            if (!rc_only)
            {
                if (o == 2)
                    Options.race = str_to_race( std::string( next_arg ) );

                if (o == 3)
                    Options.cls = str_to_class( std::string( next_arg ) );
            }
            nextUsed = true;
            break;

        case 4:             // pizza
            if (!next_is_param)
                return (false);

            if (!rc_only)
                SysEnv.crawl_pizza = next_arg;

            nextUsed = true;
            break;

        case 5:             // plain
            if (next_is_param)
                return (false);

            if (!rc_only)
            {
                Options.char_set = CSET_ASCII;
                init_char_table(Options.char_set);
            }
            break;

        case 6:             // dir
            // ALWAYS PARSE
            if (!next_is_param)
                return (false);

            SysEnv.crawl_dir = next_arg;
            nextUsed = true;
            break;

        case 7:
            // ALWAYS PARSE
            if (!next_is_param)
                return (false);

            SysEnv.crawl_rc = next_arg;
            nextUsed = true;
            break;
        } // end switch -- which option?

        // update position
        current++;
        if (nextUsed)
            current++;
    }

    return (true);
}

//////////////////////////////////////////////////////////////////////////
// game_options

int game_options::o_int(const char *name, int def) const
{
    int val = def;
    opt_map::const_iterator i = named_options.find(name);
    if (i != named_options.end())
    {
        val = atoi(i->second.c_str());
    }
    return (val);
}

long game_options::o_long(const char *name, long def) const
{
    long val = def;
    opt_map::const_iterator i = named_options.find(name);
    if (i != named_options.end())
    {
        const char *s = i->second.c_str();
        char *es = NULL;
        long num = strtol(s, &es, 10);
        if (s != (const char *) es && es)
            val = num;
    }
    return (val);
}

bool game_options::o_bool(const char *name, bool def) const
{
    bool val = def;
    opt_map::const_iterator i = named_options.find(name);
    if (i != named_options.end())
        val = read_bool(i->second, val);
    return (val);
}

std::string game_options::o_str(const char *name, const char *def) const
{
    std::string val;
    opt_map::const_iterator i = named_options.find(name);
    if (i != named_options.end())
        val = i->second;
    else if (def)
        val = def;
    return (val);
}

int game_options::o_colour(const char *name, int def) const
{
    std::string val = o_str(name);
    trim_string(val);
    tolower_string(val);
    int col = str_to_colour(val);
    return (col == -1? def : col);
}
