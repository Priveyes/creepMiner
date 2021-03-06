﻿#include "Account.hpp"
#include <mutex>
#include <Poco/Mutex.h>
#include "Wallet.hpp"
//#include "Wallet.hpp"
#include <thread>
#include "nxt/nxt_address.h"

Burst::Account::Account()
	: Account {0}
{}

Burst::Account::Account(AccountId id)
	: id_{id},
	  wallet_{nullptr},
	  getName_{this, &Account::runGetName},
	  getRewardRecipient_ {this, &Account::runGetRewardRecipient}
{}

Burst::Account::Account(const Wallet& wallet, AccountId id, bool fetchAll)
	: id_{id},
	  wallet_{&wallet},
	  getName_{this, &Account::runGetName},
	  getRewardRecipient_ {this, &Account::runGetRewardRecipient}
{
	if (fetchAll)
	{
		getNameAsync(true);
		getRewardRecipientAsync();
	}
}

void Burst::Account::setWallet(const Wallet& wallet)
{
	Poco::Mutex::ScopedLock lock{ mutex_ };
	wallet_ = &wallet;
}

Burst::AccountId Burst::Account::getId() const
{
	Poco::Mutex::ScopedLock lock{ mutex_ };
	return id_;
}

template <typename T>
const T& getHelper(Poco::Nullable<T>& val, bool reset, Poco::Mutex& mutex, std::function<bool(T&)> fetchFunction)
{
	Poco::ScopedLockWithUnlock<Poco::Mutex> lock{ mutex };

	// delete cached name if resetflag is set
	if (reset && !val.isNull())
		val.clear();

	// add name if there is no cached one
	if (val.isNull())
	{
		// because the fetching of the name is async
		// we insert an empty name in the map
		val = T{};

		lock.unlock();

		T fetchedVal;

		if (fetchFunction(fetchedVal))
		{
			Poco::Mutex::ScopedLock innerLock{ mutex };
			val = fetchedVal;
		}
		else
		{
			Poco::Mutex::ScopedLock innerLock{ mutex };
			val.clear();
		}
	}

	Poco::Mutex::ScopedLock innerLock{ mutex };
	return val;
}

std::string Burst::Account::getName()
{
	Poco::Mutex::ScopedLock lock{ mutex_ };

	if (name_.isNull() && wallet_->isActive())
		getNameAsync();

	return name_.value("");
}

Poco::ActiveResult<std::string> Burst::Account::getNameAsync(bool reset)
{
	return getName_(reset);
}

Burst::AccountId Burst::Account::getRewardRecipient()
{
	Poco::ScopedLock<Poco::Mutex> lock{ mutex_ };

	if (!wallet_->isActive())
		return rewardRecipient_.value(0);

	if (rewardRecipient_.isNull())
		getRewardRecipientAsync();

	return rewardRecipient_.value(0);
}

Poco::ActiveResult<Burst::AccountId> Burst::Account::getRewardRecipientAsync(bool reset)
{
	return getRewardRecipient_(reset);
}

std::string Burst::Account::getAddress() const
{
	return NxtAddress(getId()).to_string();
}

Poco::JSON::Object::Ptr Burst::Account::toJSON() const
{
	Poco::Mutex::ScopedLock lock{ mutex_ };

	Poco::JSON::Object::Ptr json(new Poco::JSON::Object);

	json->set("numeric", getId());
	json->set("address", getAddress());

	auto name = name_.value("");

	if (!name.empty())
		json->set("name", name);

	return json;
}

std::string Burst::Account::runGetName(const bool& reset)
{
	if (!wallet_->isActive())
		return "";

	return getHelper<std::string>(name_, reset, mutex_, [this](std::string& name)
	{
		return wallet_->getNameOfAccount(id_, name);
	});
}

Burst::AccountId Burst::Account::runGetRewardRecipient(const bool& reset)
{
	if (!wallet_->isActive())
		return 0;

	return getHelper<AccountId>(rewardRecipient_, reset, mutex_, [this](AccountId& recipient)
	{
		return wallet_->getRewardRecipientOfAccount(id_, recipient);
	});
}

std::shared_ptr<Burst::Account> Burst::Accounts::getAccount(AccountId id, Wallet& wallet, bool persistent)
{
	Poco::FastMutex::ScopedLock lock{ mutex_ };

	auto iter = accounts_.find(id);

	// if the account is not in the cache, we have to fetch him
	if (iter == accounts_.end())
	{
		auto account = std::make_shared<Account>(wallet, id);

		// save the account in the cache if wanted
		if (persistent)
			accounts_.emplace(id, account);

		return account;
	}

	// account is in the cache already
	return accounts_[id];
}

bool Burst::Accounts::isLoaded(AccountId id) const
{
	Poco::FastMutex::ScopedLock lock{ mutex_ };
	return accounts_.find(id) != accounts_.end();
}
