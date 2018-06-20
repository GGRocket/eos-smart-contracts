#include <eosiolib/eosio.hpp>
#include <eosiolib/multi_index.hpp>

using namespace eosio;

class ggrocket : public eosio::contract {
public:

    ggrocket(account_name self)
    : contract(self), _offers(self, self), _accounts(self, self) {
    }

    void _basiccheck(account_name self) {
        require_auth(self);
        if (!is_account_exist(self)) {
            _addaccount(self);
        }
        change();
    }

    /// @abi action
    void addoffer(account_name self, uint64_t price) {
        _basiccheck(self);

        auto cur_account_itr = _accounts.find(self);
        _accounts.modify( cur_account_itr, 0, [&](auto& a) {
            eosio_assert(a.balance >= price, "insufficient balance");
            a.frozen_balance += price;
            a.balance -= price;
        });
        
        _offers.emplace(get_self(), [&](auto& o) {
            o.id = _offers.available_primary_key();
            o.buyer = self;
            o.seller = N(none);
            o.price = price;
            o.sellerdone = 0;
            o.buyerdone = 0;
//            o.arbitordecisions = 0;
            o.hasresolved = false;
        });
    }
    
//    void getoffers(account_name self) {
//        _basiccheck(self);
//
//        vector<offer> results = get_all_offers();
//        
//        for( const auto& item : results ) {
//            // TODO
//            print(" IDs=", item.id, "\n");
//        }
//    };

    void respondoffer(account_name self, uint64_t id) {
        _basiccheck(self);
        
        auto cur_offer_itr = _offers.find(id);
        eosio_assert(cur_offer_itr != _offers.end(), "Offer is not exist");

        eosio_assert(cur_offer_itr->hasresolved == false, "Offer is already resolved");
        eosio_assert(cur_offer_itr->buyer != self, "Buyer can not be you");
        eosio_assert(cur_offer_itr->seller != self, "Seller is already you");
        eosio_assert(cur_offer_itr->seller == N(none), "Offer is reserved");
        
        _offers.modify( cur_offer_itr, 0, [&](auto& a) {
            a.seller = self;
        });
    }

    void decisoffer(account_name self, uint64_t id, int8_t decision) {
        _basiccheck(self);
        
        auto cur_offer_itr = _offers.find(id);
        eosio_assert(cur_offer_itr != _offers.end(), "Offer is not exist");
        eosio_assert(cur_offer_itr->hasresolved == false, "Offer is already resolved");
        
        if (cur_offer_itr->seller == self) {
            eosio_assert(cur_offer_itr->seller != N(none), "Offer is not reserved");
            eosio_assert(cur_offer_itr->sellerdone == 0, "You have a done status at this offer");

            _offers.modify( cur_offer_itr, 0, [&](auto& a) {
                a.sellerdone = decision;
            });
        }
        else if (cur_offer_itr->buyer == self) {
            eosio_assert(cur_offer_itr->seller != N(none), "Offer is not reserved yet");
            eosio_assert(cur_offer_itr->sellerdone != 0, "Seller has not done offer");
            eosio_assert(cur_offer_itr->buyerdone == 0, "You have a done status at this offer");
            
            _offers.modify( cur_offer_itr, 0, [&](auto& a) {
                a.buyerdone = decision;
                if (a.sellerdone == a.buyerdone) {
                   a.hasresolved = true;
                   if (decision > 0) {
                       _transferfrozen(a.buyer, a.seller, a.price);
                   }
                   else {
                       _unfrozen(a.buyer, a.price);
                   }
                }
            });
        }
        else {
            eosio_assert(cur_offer_itr->seller != N(none), "Offer is not reserved yet");
            eosio_assert(cur_offer_itr->buyerdone != cur_offer_itr->sellerdone, "Offer is not needed for arbitors");
            
            auto cur_account_itr = _accounts.find(self);
            // Check for existing did befode in _basiccheck().
            // 0 for develop. In future we set 2.8 value.
            eosio_assert(cur_account_itr->rating >= 0, "Arbitor account has not a needed rating");
            
            for( const auto& itemdec : cur_offer_itr->arbitordecisions ) {
                eosio_assert(itemdec.arbitor != self, "Arbitor has already voted");
            }
            
            
            _offers.modify( cur_offer_itr, 0, [&](auto& a) {
                arbitordone item;
                item.arbitor = self;
                item.done = decision;
                a.arbitordecisions.push_back(item);
                
                int8_t final_decision = _arbitordecisioncomplete(a);
                if (final_decision != 0) {
                    a.hasresolved = true;
                    if (final_decision > 0) {
                        _transferfrozen(a.buyer, a.seller, a.price);
                    }
                    else {
                        _unfrozen(a.buyer, a.price);
                    }
                }
            });
        }
    }


private:

    struct arbitordone {
        account_name arbitor;
        int8_t done;
    };
    /// @abi table offers i64
    struct offer {
        uint64_t id;
        account_name buyer;
        account_name seller;
        uint64_t price;
        int8_t sellerdone;
        int8_t buyerdone;
        vector<arbitordone> arbitordecisions;
        bool hasresolved;

        uint64_t primary_key()const {
            return id;
        }

    };

    typedef eosio::multi_index<N(offers), offer> offers;
    offers _offers;
    
    vector<offer> get_all_offers() {
        vector<offer> results;
        
        for( const auto& item : _offers ) {
            if (item.seller == N(none)) {
                results.push_back(item);
            }
        }
        
        return results;
    }
    
    

    /// @abi table accounts i64
    struct account {
        account_name owner;
        uint64_t balance;
        uint64_t frozen_balance;
        uint64_t rating;

        uint64_t primary_key()const {
            return owner;
        }
    };

    typedef eosio::multi_index<N(accounts), account> accounts;
    accounts _accounts;

    bool is_account_exist(account_name owner) {
        auto cur_account_itr = _accounts.find(owner);
        return cur_account_itr != _accounts.end();
    }

    void _addaccount(account_name owner) {
        _accounts.emplace(get_self(), [&](auto& a) {
            a.owner = owner;
            if (has_auth(_self)) {
                a.balance = 300000000;
            }
            else {
                a.balance = 0;
            }
            a.frozen_balance = 0;
            a.rating = 0;
        });
    }

    uint8_t _arbitordecisioncomplete(offer& a) {
        int8_t decision = 0;
        if (a.arbitordecisions.size() >= 3) {
            for( const auto& itemdec : a.arbitordecisions ) {
                decision += itemdec.done;
            }
        }

        if (decision > 0) {
            decision = 1;
        }
        else if (decision < 0) {
            decision = -1;
        }

        if (decision != 0) {
            for( const auto& itemdec : a.arbitordecisions ) {
                auto cur_account_itr = _accounts.find(itemdec.arbitor);
                _accounts.modify( cur_account_itr, 0, [&](auto& acc) {
                    if (itemdec.done == decision) {
                        acc.rating++;
                    }
                    else if (acc.rating > 0) {
                        acc.rating--;
                    }
                });
                
            }
        }
        
            
        return decision;
        
    }
    
    void _transferfrozen(account_name from, account_name to, uint64_t quantity) {
        auto cur_from_itr = _accounts.find(from);
        eosio_assert(cur_from_itr->frozen_balance >= quantity, "insufficient frozen balance");
        
        _accounts.modify( cur_from_itr, 0, [&](auto& acc) {
            acc.frozen_balance -= quantity;
        });
        
        auto cur_to_itr = _accounts.find(to);
        _accounts.modify( cur_to_itr, 0, [&](auto& acc) {
            acc.balance += quantity;
        });
        
    }
    void _unfrozen(account_name from, uint64_t quantity) {
        auto cur_from_itr = _accounts.find(from);
        eosio_assert(cur_from_itr->frozen_balance >= quantity, "insufficient frozen balance");
        
        _accounts.modify( cur_from_itr, 0, [&](auto& acc) {
            acc.frozen_balance -= quantity;
            acc.balance += quantity;
        });
       
    }

};

EOSIO_ABI(ggrocket, (addoffer)(respondoffer)(decisoffer))

