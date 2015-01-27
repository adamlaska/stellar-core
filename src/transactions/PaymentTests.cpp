// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the ISC License. See the COPYING file at the top-level directory of
// this distribution or at http://opensource.org/licenses/ISC
#include "main/Application.h"
#include "util/Timer.h"
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "util/Logging.h"
#include "crypto/Base58.h"
#include "lib/json/json.h"
#include "TxTests.h"
#include "database/Database.h"
#include "ledger/LedgerMaster.h"
#include "ledger/LedgerDelta.h"
#include "transactions/PaymentFrame.h"
#include "transactions/ChangeTrustTxFrame.h"

using namespace stellar;
using namespace stellar::txtest;


typedef std::unique_ptr<Application> appPtr;

// *STR Payment 
// *Credit Payment
// STR -> Credit Payment
// Credit -> STR Payment
// Credit -> STR -> Credit Payment
// Credit -> Credit -> Credit -> Credit Payment
// path payment where there isn't enough in the path
// path payment with a transfer rate
TEST_CASE("payment", "[tx][payment]")
{
    Config const& cfg = getTestConfig();
    Config cfg2(cfg);
    //cfg2.DATABASE = "sqlite3://test.db";
    //cfg2.DATABASE = "postgresql://dbmaster:-island-@localhost/hayashi";
    

    VirtualClock clock;
    Application app(clock, cfg2);
    app.start();

    // set up world
    SecretKey root = getRoot();
    SecretKey a1 = getAccount("A");
    SecretKey b1 = getAccount("B");

    uint64_t txfee = app.getLedgerMaster().getTxFee();

    const uint64_t paymentAmount = (uint64_t)app.getLedgerMaster().getMinBalance(0);

    // create an account
    applyPaymentTx(app, root, a1, 1, paymentAmount);
    
    AccountFrame a1Account, rootAccount;
    REQUIRE(app.getDatabase().loadAccount(root.getPublicKey(), rootAccount));
    REQUIRE(app.getDatabase().loadAccount(a1.getPublicKey(), a1Account));
    REQUIRE(rootAccount.getMasterWeight() == 1);
    REQUIRE(rootAccount.getHighThreshold() == 0);
    REQUIRE(rootAccount.getLowThreshold() == 0);
    REQUIRE(rootAccount.getMidThreshold() == 0);
    REQUIRE(a1Account.getBalance() == paymentAmount);
    REQUIRE(a1Account.getMasterWeight() == 1);
    REQUIRE(a1Account.getHighThreshold() == 0);
    REQUIRE(a1Account.getLowThreshold() == 0);
    REQUIRE(a1Account.getMidThreshold() == 0);
    REQUIRE(rootAccount.getBalance() == (100000000000000 - paymentAmount - txfee));

    const uint64_t morePayment = paymentAmount / 2;

    SECTION("send STR to an existing account")
    {
        applyPaymentTx(app, root, a1, 2, morePayment);
        
        AccountFrame a1Account2, rootAccount2;
        REQUIRE(app.getDatabase().loadAccount(root.getPublicKey(), rootAccount2));
        REQUIRE(app.getDatabase().loadAccount(a1.getPublicKey(), a1Account2));
        REQUIRE(a1Account2.getBalance() == a1Account.getBalance() + morePayment);
        REQUIRE(rootAccount2.getBalance() == (rootAccount.getBalance() - morePayment - txfee));
    }

    SECTION("send to self")
    {
        applyPaymentTx(app, root, root, 2, morePayment);

        AccountFrame rootAccount2;
        REQUIRE(app.getDatabase().loadAccount(root.getPublicKey(), rootAccount2));
        REQUIRE(rootAccount2.getBalance() == (rootAccount.getBalance() - txfee));
    }

    SECTION("send too little STR to new account (below reserve)")
    {
        LOG(INFO) << "send too little STR to new account (below reserve)";
        applyPaymentTx(app,root, b1, 2,
            app.getLedgerMaster().getCurrentLedgerHeader().baseReserve -1,Payment::UNDERFUNDED);

        AccountFrame bAccount;
        REQUIRE(!app.getDatabase().loadAccount(b1.getPublicKey(), bAccount));
    }

    SECTION("simple credit")
    {
        Currency currency=makeCurrency(root,"IDR");

        SECTION("credit sent to new account (no account error)")
        {
            LOG(INFO) << "credit sent to new account (no account error)";
            applyCreditPaymentTx(app,root, b1, currency, 2, 100, Payment::NO_DESTINATION);

            AccountFrame bAccount;
            REQUIRE(!app.getDatabase().loadAccount(b1.getPublicKey(), bAccount));
        }

        SECTION("send STR with path (not enough offers)")
        {
            LOG(INFO) << "send STR with path";
            TransactionFramePtr txFrame2 = createPaymentTx(root, a1, 2, morePayment);
            txFrame2->getEnvelope().tx.body.paymentTx().path.push_back(currency);
            LedgerDelta delta2;
            txFrame2->apply(delta2, app);

            REQUIRE(Payment::getInnerCode(txFrame2->getResult()) == Payment::OVERSENDMAX);
            AccountFrame account;
            REQUIRE(app.getDatabase().loadAccount(a1.getPublicKey(), account));
            
        }

        // actual sendcredit
        SECTION("credit payment with no trust")
        {
            LOG(INFO) << "credit payment with no trust";
            applyCreditPaymentTx(app,root, a1, currency, 2, 100, Payment::NO_TRUST);
            AccountFrame account;
            REQUIRE(app.getDatabase().loadAccount(a1.getPublicKey(), account));
           
        }

        SECTION("with trust")
        {
            LOG(INFO) << "with trust";

            applyTrust(app, a1, root, 1, "IDR");
            applyCreditPaymentTx(app, root, a1, currency, 2, 100);

            TrustFrame line;
            REQUIRE(app.getDatabase().loadTrustLine(a1.getPublicKey(), currency, line));
            REQUIRE(line.getBalance() == 100);

            // create b1 account
            applyPaymentTx(app,root, b1, 3, paymentAmount);
            applyTrust(app,b1, root, 1, "IDR");
            applyCreditPaymentTx(app,a1, b1, currency, 2, 40);
               
            REQUIRE(app.getDatabase().loadTrustLine(a1.getPublicKey(), currency, line));
            REQUIRE(line.getBalance() == 60);
            REQUIRE(app.getDatabase().loadTrustLine(b1.getPublicKey(), currency, line));
            REQUIRE(line.getBalance() == 40);
            applyCreditPaymentTx(app,b1, root, currency, 2, 40);
            REQUIRE(app.getDatabase().loadTrustLine(b1.getPublicKey(), currency, line));
            REQUIRE(line.getBalance() == 0);
        }
    }
}



