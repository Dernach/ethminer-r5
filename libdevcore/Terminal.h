#pragma once

namespace dev
{
namespace con
{

/**
 * @brief ANSI terminal color and formatting codes
 *
 * These macros define ANSI escape sequences for terminal text formatting.
 * They can be used to add color and style to console output.
 */

/** Text formatting reset */
#define EthReset "\x1b[0m"

/** Regular text colors */
#define EthBlack "\x1b[30m"   ///< Black text
#define EthCoal "\x1b[90m"    ///< Dark gray text
#define EthGray "\x1b[37m"    ///< Light gray text
#define EthWhite "\x1b[97m"   ///< White text
#define EthMaroon "\x1b[31m"  ///< Dark red text
#define EthRed "\x1b[91m"     ///< Bright red text
#define EthGreen "\x1b[32m"   ///< Dark green text
#define EthLime "\x1b[92m"    ///< Bright green text
#define EthOrange "\x1b[33m"  ///< Dark yellow/orange text
#define EthYellow "\x1b[93m"  ///< Bright yellow text
#define EthNavy "\x1b[34m"    ///< Dark blue text
#define EthBlue "\x1b[94m"    ///< Bright blue text
#define EthViolet "\x1b[35m"  ///< Dark purple/violet text
#define EthPurple "\x1b[95m"  ///< Bright purple text
#define EthTeal "\x1b[36m"    ///< Dark cyan/teal text
#define EthCyan "\x1b[96m"    ///< Bright cyan text

/** Bold text colors */
#define EthBlackBold "\x1b[1;30m"   ///< Bold black text
#define EthCoalBold "\x1b[1;90m"    ///< Bold dark gray text
#define EthGrayBold "\x1b[1;37m"    ///< Bold light gray text
#define EthWhiteBold "\x1b[1;97m"   ///< Bold white text
#define EthMaroonBold "\x1b[1;31m"  ///< Bold dark red text
#define EthRedBold "\x1b[1;91m"     ///< Bold bright red text
#define EthGreenBold "\x1b[1;32m"   ///< Bold dark green text
#define EthLimeBold "\x1b[1;92m"    ///< Bold bright green text
#define EthOrangeBold "\x1b[1;33m"  ///< Bold dark yellow/orange text
#define EthYellowBold "\x1b[1;93m"  ///< Bold bright yellow text
#define EthNavyBold "\x1b[1;34m"    ///< Bold dark blue text
#define EthBlueBold "\x1b[1;94m"    ///< Bold bright blue text
#define EthVioletBold "\x1b[1;35m"  ///< Bold dark purple/violet text
#define EthPurpleBold "\x1b[1;95m"  ///< Bold bright purple text
#define EthTealBold "\x1b[1;36m"    ///< Bold dark cyan/teal text
#define EthCyanBold "\x1b[1;96m"    ///< Bold bright cyan text

/** Background colors */
#define EthOnBlack "\x1b[40m"    ///< Black background
#define EthOnCoal "\x1b[100m"    ///< Dark gray background
#define EthOnGray "\x1b[47m"     ///< Light gray background
#define EthOnWhite "\x1b[107m"   ///< White background
#define EthOnMaroon "\x1b[41m"   ///< Dark red background
#define EthOnRed "\x1b[101m"     ///< Bright red background
#define EthOnGreen "\x1b[42m"    ///< Dark green background
#define EthOnLime "\x1b[102m"    ///< Bright green background
#define EthOnOrange "\x1b[43m"   ///< Dark yellow/orange background
#define EthOnYellow "\x1b[103m"  ///< Bright yellow background
#define EthOnNavy "\x1b[44m"     ///< Dark blue background
#define EthOnBlue "\x1b[104m"    ///< Bright blue background
#define EthOnViolet "\x1b[45m"   ///< Dark purple/violet background
#define EthOnPurple "\x1b[105m"  ///< Bright purple background
#define EthOnTeal "\x1b[46m"     ///< Dark cyan/teal background
#define EthOnCyan "\x1b[106m"    ///< Bright cyan background

/** Underlined text colors */
#define EthBlackUnder "\x1b[4;30m"   ///< Underlined black text
#define EthGrayUnder "\x1b[4;37m"    ///< Underlined light gray text
#define EthMaroonUnder "\x1b[4;31m"  ///< Underlined dark red text
#define EthGreenUnder "\x1b[4;32m"   ///< Underlined dark green text
#define EthOrangeUnder "\x1b[4;33m"  ///< Underlined dark yellow/orange text
#define EthNavyUnder "\x1b[4;34m"    ///< Underlined dark blue text
#define EthVioletUnder "\x1b[4;35m"  ///< Underlined dark purple/violet text
#define EthTealUnder "\x1b[4;36m"    ///< Underlined dark cyan/teal text

}  // namespace con
}  // namespace dev
