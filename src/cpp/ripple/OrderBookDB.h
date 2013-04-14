
#ifndef ORDERBOOK_DB_H
#define ORDERBOOK_DB_H

#include <boost/shared_ptr.hpp>
#include <boost/unordered_map.hpp>

#include "Ledger.h"
#include "AcceptedLedger.h"
#include "OrderBook.h"


//
// XXX Eventually make this cached and just update it as transactions come in.
// But, for now it is probably faster to just generate it each time.
//

typedef std::pair<uint160, uint160> currencyIssuer_t;
typedef std::pair<const uint160&, const uint160&> currencyIssuer_ct;

class BookListeners
{
	boost::unordered_map<uint64, InfoSub::wptr> mListeners;
	boost::recursive_mutex mLock;

public:
	typedef boost::shared_ptr<BookListeners> pointer;

	void addSubscriber(InfoSub::ref sub);
	void removeSubscriber(uint64 sub);
	void publish(Json::Value& jvObj);
};

class OrderBookDB
{
	boost::unordered_map< currencyIssuer_t, std::vector<OrderBook::pointer> > mSourceMap;	// by ci/ii
	boost::unordered_map< currencyIssuer_t, std::vector<OrderBook::pointer> > mDestMap;		// by co/io

	// issuerPays, issuerGets, currencyPays, currencyGets
	std::map<uint160, std::map<uint160, std::map<uint160, std::map<uint160, BookListeners::pointer> > > > mListeners;

	uint32 mSeq;
	boost::recursive_mutex mLock;

public:
	OrderBookDB();
	void setup(Ledger::ref ledger);
	void invalidate();

	// return list of all orderbooks that want this issuerID and currencyID
	void getBooksByTakerPays(const uint160& issuerID, const uint160& currencyID,
		std::vector<OrderBook::pointer>& bookRet);
	void getBooksByTakerGets(const uint160& issuerID, const uint160& currencyID,
		std::vector<OrderBook::pointer>& bookRet);

	BookListeners::pointer getBookListeners(const uint160& currencyPays, const uint160& currencyGets,
		const uint160& issuerPays, const uint160& issuerGets);
	BookListeners::pointer makeBookListeners(const uint160& currencyPays, const uint160& currencyGets,
		const uint160& issuerPays, const uint160& issuerGets);

	// see if this txn effects any orderbook
	void processTxn(Ledger::ref ledger, const ALTransaction& alTx, Json::Value& jvObj);
};

#endif

// vim:ts=4