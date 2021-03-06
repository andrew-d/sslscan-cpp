#ifndef OPTIONPARSER_H
#define OPTIONPARSER_H

#include "Expected.hpp"

#include <string>
#include <vector>
#include <functional>

#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/device/array.hpp>

// The base class for errors.
class OptionParserError : public std::runtime_error {
protected:
    enum { BUFFER_SIZE = 200 };
    char m_whatText[BUFFER_SIZE];

    // This allows derived classes to stop the printing to
    // a buffer.
    // TODO: is there a better way to do this?
    OptionParserError(const char* underlying, bool)
        : std::runtime_error(underlying)
    { }

public:
    OptionParserError(const char* option) :
        std::runtime_error("option error")
    {
        boost::iostreams::stream<boost::iostreams::array_sink> out(m_whatText, BUFFER_SIZE);
        out << "error with option: " << option << std::ends;
    }

    virtual ~OptionParserError() throw() {}

    virtual const char* what() const noexcept override {
        return m_whatText;
    }
};

// This error gets thrown when an argument for a switch with a
// required argument is missing.
class MissingArgumentError : public OptionParserError {
public:
    MissingArgumentError(const char* option)
        : OptionParserError("option error: missing argument", false)
    {
        boost::iostreams::stream<boost::iostreams::array_sink> out(m_whatText, BUFFER_SIZE);
        out << "option " << option << " is missing the required argument" << std::ends;
    }
};

// This error gets thrown when an invalid switch is encountered.
class InvalidSwitchError : public OptionParserError {
public:
    InvalidSwitchError(const char* switch_)
        : OptionParserError("option error: invalid switch", false)
    {
        boost::iostreams::stream<boost::iostreams::array_sink> out(m_whatText, BUFFER_SIZE);
        out << "switch " << switch_ << " not recognized" << std::ends;
    }
};

// This error gets thrown when a short option with an argument is
// found in a larger collection of option strings.  E.g.:
//    ./foo -abc        where "-c" takes an argument
class InvalidPositionError : public OptionParserError {
public:
    InvalidPositionError(const char* option)
        : OptionParserError("option error: invalid position for switch", false)
    {
        boost::iostreams::stream<boost::iostreams::array_sink> out(m_whatText, OptionParserError::BUFFER_SIZE);
        out << "switch " << option << " is in an invalid position" << std::ends;
    }
};


class OptionSwitch {
public:
    typedef std::function<void(const std::string& arg)> CallbackType;
    typedef std::function<void()>                       VoidCallbackType;

private:
    std::string  m_shortOpt;
    std::string  m_longOpt;
    CallbackType m_cb;
    bool         m_hasParameter;
    bool         m_parameterOptional;
    std::string  m_helpText;

public:
    // This is the canonical constructor.
    OptionSwitch(std::string shortOpt,
                 std::string longOpt = "")
        : m_shortOpt(shortOpt), m_longOpt(longOpt) { }

    // Help text
    // --------------------------------------------------
    OptionSwitch& SetHelpText(std::string& txt) {
        m_helpText = txt;
        return *this;
    }
    const std::string& GetHelpText() const {
        return m_helpText;
    }

    // Option(s)
    // --------------------------------------------------
    inline bool HasShortOpt() const {
        return m_shortOpt.length() > 0;
    }
    inline const std::string& ShortOpt() const {
        return m_shortOpt;
    }

    inline bool HasLongOpt() const {
        return m_longOpt.length() > 0;
    }
    inline const std::string& LongOpt() const {
        return m_longOpt;
    }

    // Parameter
    // --------------------------------------------------
    OptionSwitch& SetParameter(bool param) {
        m_hasParameter = param;
        return *this;
    }
    inline bool HasParameter() const {
        return m_hasParameter;
    }

    OptionSwitch& SetParameterOptional(bool opt) {
        m_parameterOptional = opt;
        return *this;
    }
    inline bool ParamOptional() const {
        return m_parameterOptional;
    }

    // Callback
    // --------------------------------------------------
    OptionSwitch& SetCallback(CallbackType cb) {
        m_cb = cb;
        return *this;
    }
    OptionSwitch& SetCallback(VoidCallbackType cb) {
        m_cb = [=](const std::string&) -> void {
            cb();
        };
        return *this;
    }
    const CallbackType GetCallback() const {
        return m_cb;
    }
    void CallCallback(const std::string& arg) {
        return m_cb(arg);
    }
    void CallCallback() {
        return m_cb("");
    }
};


class OptionParser {
private:
    std::vector< OptionSwitch > m_options;

public:
    OptionParser() { }

    Expected<std::vector<std::string>> Parse(int argc, char* argv[]) {
        // This is the vector range constructor.
        return Parse(std::vector< std::string>(argv + 1, argv + argc));
    }

    Expected<std::vector<std::string>> Parse(std::vector< std::string > args) {
        std::vector< std::string > rest;
        size_t i = 0;

        // This is a helper "function" that abstracts out parsing an argument
        // with a parameter.
        auto parseArgument = [&args, &i](OptionSwitch& arg) {
            if( arg.HasParameter() ) {
                // Has a param.  See if we have the param as being available.
                if( (i + 1) < args.size() ) {
                    // Is available.  Trigger callback with args[i + 1].
                    i += 1;
                    arg.CallCallback(args[i]);
                } else if( arg.ParamOptional() ) {
                    // Param is not available, is optional.  Trigger callback.
                    arg.CallCallback();
                } else {
                    // Param is not available, not optional.  Error.
                    throw MissingArgumentError(args[i].c_str());
                }
            } else {
                // No param.  Trigger callback.
                arg.CallCallback();
            }
        };

        for( ; i < args.size(); i++ ) {
            std::string& curr = args[i];

            if( "--" == curr ) {
                // Point the counter past this argument.
                i++;
                break;
            }
            if( curr.length() > 0 && curr[0] == '-' ) {
                if( curr.length() < 2 ) {
                    // Too short - don't know what this is supposed to be.
                    return Expected<std::vector<std::string>>::fromException(
                                InvalidSwitchError(curr.c_str())
                            );
                }

                if( curr[1] == '-' ) {
                    // It's a long option.
                    bool found = false;
                    for( auto opt : m_options ) {
                        if( opt.HasLongOpt() &&
                            0 == curr.compare(2, curr.size() - 2, opt.LongOpt()) )
                        {
                            found = true;
                            try {
                                parseArgument(opt);
                            } catch( const MissingArgumentError& e ) {
                                return Expected<std::vector<std::string>>::fromException(e);
                            }
                            break;
                        }
                    }

                    if( !found ) {
                        return Expected<std::vector<std::string>>::fromException(
                                    InvalidSwitchError(curr.c_str())
                                );
                    }
                } else {
                    // It's a short option, or sequence thereof.

                    // The short options are only allowed to have a parameter when
                    // they are by themselves - i.e. "-a foo" is okay, "-va foo" is
                    // not.  We validate this here.
                    if( 2 == curr.length() ) {
                        bool found = false;
                        for( auto opt : m_options ) {
                            if( opt.HasShortOpt() && opt.ShortOpt()[0] == curr[1] )
                            {
                                found = true;
                                try {
                                    parseArgument(opt);
                                } catch( const MissingArgumentError& e ) {
                                    return Expected<std::vector<std::string>>::fromException(e);
                                }
                                break;
                            }
                        }

                        if( !found ) {
                            return Expected<std::vector<std::string>>::fromException(
                                        InvalidSwitchError(curr.c_str()));
                        }
                    } else {
                        // Longer than 2 characters - need to iterate over each character.
                        for( size_t j = 1; j < curr.length(); j++ ) {
                            char ch = curr[1 + j];

                            // Validate that this argument exists, and does not have a param
                            bool found = false;
                            for( auto opt : m_options ) {
                                if( opt.HasShortOpt() && opt.ShortOpt()[0] == ch )
                                {
                                    // If this option would have a parameter, then it's an error.
                                    if( opt.HasParameter() ) {
                                        char temp_buff[3] = {'-', ch, '\0'};
                                        return Expected<std::vector<std::string>>::fromException(
                                                InvalidPositionError(temp_buff));
                                    }

                                    found = true;
                                    try {
                                        parseArgument(opt);
                                    } catch( const MissingArgumentError& e ) {
                                        return Expected<std::vector<std::string>>::fromException(e);
                                    }
                                    break;
                                }
                            }

                            if( !found ) {
                                char temp_buff[3] = {'-', ch, '\0'};
                                return Expected<std::vector<std::string>>::fromException(
                                        InvalidSwitchError(temp_buff));
                            }
                        }
                    }
                }
            } else {
                // This isn't an argument.  We want to save it for later.
                rest.push_back(curr);
            }
        }

        // Save all arguments from i until the end of the array in 'rest'.
        auto remaining_begin = begin(args) + i;
        auto remaining_end = end(args);

        rest.reserve(rest.size() + std::distance(remaining_begin, remaining_end));
        rest.insert(end(rest), remaining_begin, remaining_end);

        return std::move(rest);
    }

    template <typename ...Args>
    OptionSwitch& On(Args&&... args) {
        m_options.emplace_back(args...);
        return m_options[m_options.size() - 1];
    }
};

#endif
