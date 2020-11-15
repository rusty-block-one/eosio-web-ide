#include <eosio/eosio.hpp>

enum class reaction_type : uint8_t {
    thumbs_up = 0,
    thumbs_down,
    meh
};

//  tally of available reactions
struct reaction_tally {
    uint64_t thumbs_up {0};
    uint64_t thumbs_down {0};
    uint64_t meh {0};
    reaction_tally& operator+=(reaction_type reaction) {
        switch(reaction) {
            case reaction_type::thumbs_up:      thumbs_up++;    break;
            case reaction_type::thumbs_down:    thumbs_down++;  break;
            case reaction_type::meh:            meh++;          break;
        }
        return *this;
    }
    reaction_tally& operator-=(reaction_type reaction) {
        switch(reaction) {
            case reaction_type::thumbs_up:      thumbs_up--;    break;
            case reaction_type::thumbs_down:    thumbs_down--;  break;
            case reaction_type::meh:            meh--;          break;
        }
        return *this;
    }
};
// Message table
struct [[eosio::table("message"), eosio::contract("talk")]] message {
    uint64_t    id       = {}; // Non-0
    uint64_t    reply_to = {}; // Non-0 if this is a reply
    eosio::name user     = {};
    std::string content  = {};
    reaction_tally stats = {}; // reactions to the talk

    uint64_t primary_key() const { return id; }
    uint64_t get_reply_to() const { return reply_to; }
};

using message_table = eosio::multi_index<"message"_n, message,
      eosio::indexed_by<"by.reply.to"_n, eosio::const_mem_fun<message, uint64_t, &message::get_reply_to>>>;

// Contributor table
struct [[eosio::table("contributors"), eosio::contract("talk")]] contributors {
    eosio::name name = {};
    std::map<uint64_t, reaction_type> reactions;

    uint64_t primary_key() const { return name.value; }
};

using contributor_table = eosio::multi_index<"contributors"_n, contributors>;

// The contract
class talk : eosio::contract {
  private:
    /// Record a new reaction for a talk
    //
    //  @param[in]  talk        The talk to update
    //  @param[in]  reaction    The reaction to update
    //  @return N/A
    void record_reaction(uint64_t talk, reaction_type reaction) {
        message_table table(get_self(), 0);
        auto& message = table.find(talk);
        check(message != table.end(), "Referenced talk does not exist");
        table.modify(message, _self, [&](auto& row) {
                row.stats += reaction;
                });
    }
    /// Change the reaction to a talk
    //
    //  @param[in]  talk    The talk to update
    //  @param[in]  from    The original reaction
    //  @param[in]  to      The new reaction
    //  @return N/A
    void change_reaction(uint64_t talk, reaction_type from, reaction_type to) {
        message_table table(get_self(), 0);
        auto& message = table.find(talk);
        check(message != table.end(), "Referenced talk does not exist");
        table.modify(message, _self, [&](auto& row) {
                row.stats -= from;
                row.stats += to;
                });
    }
    /// Process a user's reaction
    //
    //  @param[in]  user        The user posting the reaction
    //  @param[in]  reply_to    The message the user is reacting to
    //  @param[in]  reaction    The user's reaction to the message
    //  @return N/A
    void process_reaction(eosio::name user,  uint64_t reply_to, reaction_type reaction) {
        //  check user
        require_auth(user);

        //  check reply-to
        message_table table(get_self(), 0);
        auto& message table.find(reply_to);
        if(message == table.end()) {
            return;
        }

        //  if first time contributor
        reaction_table current_reactions(get_self(), 0);
        auto reaction = current_reactions.find(user.value);
        if(reaction == current_reactions.end()) {
            current_reactions.emplace(user.value, [&](auto& row) {
                    row.contributor = user;
                    reactions[reply_to] = reaction;
                    record_reaction(reply_to, reaction);
                    });
            return;
        }

        //  if existing contributor
        current_reactions.modify(reaction, _self [&](auto& row) {
                auto old = row.reactions.find(reply_to);
                //  if first time contributing to this talk
                if(old == row.reactions.end()) {
                    record_reaction(reply_to, reaction);
                    row.reactions[reply_to] = reaction;
                }
                //  if changed mind about reaction
                else if(old->second != reaction) {
                    change_reaction(reply_to, old->second, reaction);
                    old->second = reaction;
                }
            });
    }

  public:
    // Use contract's constructor
    using contract::contract;

    // Post a message
    [[eosio::action]]
    void post(uint64_t id, uint64_t reply_to, eosio::name user, const std::string& content) {
        message_table table{get_self(), 0};

        // Check user
        require_auth(user);

        // Check reply_to exists
        if (reply_to)
            table.get(reply_to);

        // Create an ID if user didn't specify one
        eosio::check(id < 1'000'000'000ull, "user-specified id is too big");
        if (!id)
            id = std::max(table.available_primary_key(), 1'000'000'000ull);

        // Record the message
        table.emplace(get_self(), [&](auto& message) {
            message.id       = id;
            message.reply_to = reply_to;
            message.user     = user;
            message.content  = content;
        });
    }

    [[eosios::action]]
    void thumbs_up(eosio::name user, uint64_t reply_to) {
        process_reaction(user, reply_to, reaction_type::thumbs_up);
    }

    [[eosios::action]]
    void thumbs_down(eosio::name user,  uint64_t reply_to) {
        process_reaction(user, reply_to, reaction_type::thumbs_down);
    }

    [[eosios::action]]
    void meh(eosio::name user,  uint64_t reply_to) {
        process_reaction(user, reply_to, reaction_type::meh);
    }

};
