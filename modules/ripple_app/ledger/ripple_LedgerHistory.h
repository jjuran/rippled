//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_LEDGERHISTORY_H
#define RIPPLE_LEDGERHISTORY_H

// VFALCO TODO Rename to OldLedgers ?
class LedgerHistory : LeakChecked <LedgerHistory>
{
public:
    LedgerHistory ();

    void addLedger (Ledger::pointer ledger);

    float getCacheHitRate ()
    {
        return mLedgersByHash.getHitRate ();
    }

    Ledger::pointer getLedgerBySeq (LedgerIndex ledgerIndex);

    // VFALCO NOTE shouldn't this call the function above?
    LedgerHash getLedgerHash (LedgerIndex ledgerIndex);

    Ledger::pointer getLedgerByHash (LedgerHash const& ledgerHash);

    Ledger::pointer canonicalizeLedger (Ledger::pointer ledger, bool cache);

    void tune (int size, int age);

    void sweep ()
    {
        mLedgersByHash.sweep ();
    }

private:
    TaggedCache <LedgerHash, Ledger, UptimeTimerAdapter> mLedgersByHash;

    // Maps ledger indexes to the corresponding hash.
    std::map <LedgerIndex, LedgerHash> mLedgersByIndex; // validated ledgers
};

#endif