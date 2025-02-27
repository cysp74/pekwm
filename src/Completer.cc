//
// Completer.cc for pekwm
// Copyright (C) 2009-2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "config.h"

#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <vector>
#include <string>

extern "C" {
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
}

#include "Charset.hh"
#include "Completer.hh"
#include "Config.hh"
#include "Util.hh"
#include "PWinObj.hh"
#include "MenuHandler.hh"

static bool starts_with(const std::string &str, size_t off,
                        const std::string &prefix, size_t prefix_len)
{
	if ((str.size() - off) < prefix_len) {
		return false;
	}
	return memcmp(str.c_str() + off, prefix.c_str(), prefix_len) == 0;
}

CompPair::CompPair(const std::string& _first,
                   const std::string& _second)
	: first(_first),
	  second(_second)
{
}

CompPair::~CompPair(void)
{
}

bool
CompPair::operator==(const CompPair& rhs) const
{
	return first == rhs.first && second == rhs.second;
}

bool
CompPair::operator<(const CompPair& rhs) const
{
	if (first == rhs.first) {
		return second < rhs.second;
	}
	return first < rhs.first;
}

bool CompPair_lt(const CompPair& lhs, const CompPair& rhs)
{
	return lhs < rhs;
}

bool CompPair_equal(const CompPair& lhs, const CompPair& rhs)
{
	return lhs == rhs;
}

/**
 * Build completions_list from a container of strings.
 */
template<typename T>
static void
completions_list_from_name_list(T name_list, completions_list &completions_list)
{
	completions_list.clear();
	typename T::const_iterator it(name_list.begin());
	for (; it != name_list.end(); ++it) {
		std::string name = *it;
		std::string name_lower(name);
		Util::to_lower(name_lower);
		completions_list.push_back(CompPair(name_lower, name));
	}
	std::unique(completions_list.begin(), completions_list.end(),
		    CompPair_equal);
	std::sort(completions_list.begin(), completions_list.end(),
		  CompPair_lt);
}

/**
 * Base class for completer methods, provides method to see if it
 * should be used and also actual completion.
 */
class CompleterMethod
{
public:
	/** Constructor for CompleterMethod, refresh completion list. */
	CompleterMethod(void) { }
	/** Destructor for CompleterMethod */
	virtual ~CompleterMethod(void) { }

	/** Find completions for string. */
	virtual unsigned int complete(CompletionState&) {
		return 0;
	}
	/** Refresh completion list. */
	virtual void refresh(void)=0;
	/** Clear completion list. */
	virtual void clear(void)=0;

protected:
	/**
	 * Find matches for word in completions_list and add to completions.
	 */
	unsigned int complete_word(completions_list &completions_list,
				   complete_list &completions,
				   const std::string &word)
	{
		int completed = 0;

		completions_it it(completions_list.begin());
		for (; it != completions_list.end(); ++it) {
			if (it->first.size() < word.size()) {
				continue;
			}
			if (starts_with(it->first, 0, word, word.size())) {
				completions.push_back(it->second);
				completed++;
			}
		}

		return completed;
	}
};

/**
 * Path completer, provides completion of elements in the path.
 */
class PathCompleterMethod : public CompleterMethod
{
public:
	/** Constructor for PathCompleter method. */
	PathCompleterMethod(void) : CompleterMethod() { refresh(); }
	/** Destructor for PathCompleterMethod */
	virtual ~PathCompleterMethod(void) { }

	/**
	 * Complete str with available path elements.
	 */
	virtual unsigned int complete(CompletionState &completion_state) {
		return complete_word(_path_list,
				     completion_state.completions,
				     completion_state.word);
	}

	void refresh(void) {
		clear();

		std::vector<std::string> path_parts;
		std::vector<std::string>::iterator it;;
		Util::splitString(Util::getEnv("PATH"), path_parts, ":");

		for (it = path_parts.begin(); it != path_parts.end(); ++it) {
			DIR *dh = opendir(it->c_str());
			if (dh) {
				refresh_path(dh, Charset::fromSystem(*it));
				closedir(dh);
			}
		}

		std::unique(_path_list.begin(), _path_list.end());
		std::sort(_path_list.begin(), _path_list.end());
	}

	void clear(void) {
		_path_list.clear();
	}

private:
	void refresh_path(DIR *dh, const std::string& path);
	void add_path(const std::string& name, const std::string& path_name);

	completions_list _path_list; /**< List of all elements in path. */
};

/**
 * Refresh single directory.
 */
void
PathCompleterMethod::refresh_path(DIR *dh, const std::string& path)
{
	struct dirent *entry;
	while ((entry = readdir(dh)) != 0) {
		if (entry->d_name[0] == '.') {
			continue;
		}

		std::string name = Charset::toSystem(entry->d_name);
		std::string path_name = path + "/" + name;
		add_path(name, path_name);
	}
}

void
PathCompleterMethod::add_path(const std::string& name,
                              const std::string& path_name)
{
	CompPair nn(name, name);
	_path_list.push_back(nn);
	CompPair pp(path_name, path_name);
	_path_list.push_back(pp);
}

/**
 * Action completer, provides completion of all available actions in
 * pekwm.
 */
class ActionCompleterMethod : public CompleterMethod
{
public:
	/**
	 * States for context sensitive ActionCompleterMethod completions.
	 */
	enum State {
		STATE_ACTION,
		STATE_STATE,
		STATE_MENU,
		STATE_NO,
		STATE_NUM = 5
	};

	/**
	 * Context match information.
	 */
	class StateMatch {
	public:
		StateMatch(State state, const char *prefix)
			: _prefix(prefix),
			  _prefix_len(strlen(prefix)),
			  _state(state) {
		}

		State get_state(void) { return _state; }
		//! Check if str matches state prefix.
		bool is_state(const std::string &str, size_t pos) {
			return (str.size() - pos < _prefix_len)
				? false
				: starts_with(str, pos, _prefix, _prefix_len);
		}
	private:
		const char *_prefix; /**< Matching prefix */
		const size_t _prefix_len;
		State _state; /**< State */
	};

	ActionCompleterMethod(void);
	virtual ~ActionCompleterMethod(void);

	virtual unsigned int complete(CompletionState &state) {
		State type_state = find_state(state);
		switch (type_state) {
		case STATE_STATE:
			return complete_word(_state_list, state.completions, state.word_lower);
		case STATE_MENU:
			return complete_word(_menu_list, state.completions, state.word_lower);
		case STATE_ACTION:
			return complete_word(_action_list, state.completions, state.word_lower);
		case STATE_NO:
		default:
			return 0;
		}
	}

	//! Build list of completions from available actions.
	virtual void refresh(void);

	void clear(void) { }

private:
	State find_state(CompletionState &completion_state);
	size_t find_state_word_start(const std::string &str);

	completions_list _action_list; /**< List of all available actions. */
	completions_list _state_list; /**< List of parameters to state actions. */
	completions_list _menu_list; /**< List of parameters to state actions. */
	/**< List of known states with matching data. */
	static StateMatch STATE_MATCHES[];
};

ActionCompleterMethod::ActionCompleterMethod(void)
	: CompleterMethod()
{
	refresh();
}

ActionCompleterMethod::~ActionCompleterMethod(void)
{
}

void
ActionCompleterMethod::refresh(void)
{
	completions_list_from_name_list(ActionConfig::getActionNameList(),
					_action_list);
	completions_list_from_name_list(ActionConfig::getStateNameList(),
					_state_list);
	completions_list_from_name_list(MenuHandler::getMenuNames(),
					_menu_list);
}

ActionCompleterMethod::StateMatch ActionCompleterMethod::STATE_MATCHES[] = {
	StateMatch(ActionCompleterMethod::STATE_STATE, "set"),
	StateMatch(ActionCompleterMethod::STATE_STATE, "unset"),
	StateMatch(ActionCompleterMethod::STATE_STATE, "toggle"),
	StateMatch(ActionCompleterMethod::STATE_MENU, "showmenu")
};

/**
 * Detect state being completed
 */
ActionCompleterMethod::State
ActionCompleterMethod::find_state(CompletionState &completion_state)
{
	if (completion_state.word_begin != 0) {
		for (unsigned i = 0; i < sizeof(STATE_MATCHES)/sizeof(StateMatch); ++i) {
			if (STATE_MATCHES[i].is_state(completion_state.part_lower, completion_state.part_begin)) {
				return STATE_MATCHES[i].get_state();
			}
		}
		return STATE_NO;
	}
	return STATE_ACTION;
}


Completer::Completer(void) : _completer_action(new ActionCompleterMethod),
			     _completer_path(new PathCompleterMethod)
{
}

Completer::~Completer(void)
{
	delete _completer_action;
	delete _completer_path;
}

/**
 * Refresh completions.
 */
void
Completer::refresh()
{
	_completer_path->refresh();
}

/**
 * Clear data used by completions.
 */
void
Completer::clear()
{
	_completer_path->clear();
}

/**
 * Find completions for string with the cursor at position.
 */
complete_list
Completer::find_completions(const std::string &str, unsigned int pos)
{
	// Get current part of str, if it is empty return no completions.
	CompletionState state;
	state.part = state.part_lower =
		get_part(str, pos, state.part_begin, state.part_end);
	if (! state.part.size()) {
		return state.completions;
	}

	// Get word at position, the one that will be completed
	state.word = state.word_lower =
		get_word_at_position(str, pos, state.word_begin, state.word_end);

	Util::to_lower(state.part_lower);
	Util::to_lower(state.word_lower);

	_completer_action->complete(state);
	_completer_path->complete(state);

	return state.completions;
}

/**
 * Perform actual completion, returns new string with completion
 * inserted if any.
 *
 * @param str String to complete.
 * @param pos Cursor position in string.
 * @return Completed string.
 */
std::string
Completer::do_complete(const std::string &str, unsigned int &pos,
                       complete_list &completions, complete_it &it)
{
	// Do not perform completion if there is nothing to complete
	if (! completions.size()) {
		pos = str.size();
		return str;
	}
	// Wrap completions, return original string
	if (it == completions.end()) {
		it = completions.begin();
		pos = str.size();
		return str;
	}

	// Get current word, this is the one being replaced
	size_t word_begin, word_end;
	std::string word(get_word_at_position(str, pos, word_begin, word_end));

	// Replace the current word
	std::string completed(str);
	completed.replace(word_begin, word_end - word_begin, *it);

	// Update position
	pos = word_begin + it->size();

	// Increment completion
	it++;

	return completed;
}

/**
 * Find current part being completed, string can be split up with
 * separators and only one part should be treated at a time.
 *
 * @param str String to find part in.
 * @param pos Position in string.
 * @param part_begin
 * @param part_end
 * @return Current part of string.
 */
std::string
Completer::get_part(const std::string &str, unsigned int pos,
                    size_t &part_begin, size_t &part_end)
{
	// Get beginning and end of string, add 1 for removal of separator
	part_begin = StringUtil::safe_position(str.find_last_of(";", pos), 0, 1);
	part_end = StringUtil::safe_position(str.find_first_of(";", pos), str.size());

	// Strip spaces from the beginning of the string
	part_begin =
		StringUtil::safe_position(str.find_first_not_of(" \t", part_begin),
					  part_end);

	return str.substr(part_begin, part_end - part_begin);
}

/**
 * Get word at position.
 */
std::string
Completer::get_word_at_position(const std::string &str, unsigned int pos,
                                size_t &word_begin, size_t &word_end)
{
	// Get beginning and end of string, add 1 for removal of separator
	word_begin = StringUtil::safe_position(str.find_last_of(" \t", pos), 0, 1);
	word_end =
		StringUtil::safe_position(str.find_first_of(" \t", pos), str.size());

	return str.substr(word_begin, word_end - word_begin);
}
