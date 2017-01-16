#ifndef INCO_NIOCS_TEST_SIMPLESTOCKCLIENT_HXX
#define INCO_NIOCS_TEST_SIMPLESTOCKCLIENT_HXX

#include <cppuhelper/implbase2.hxx>
#include <inco/niocs/test/XStockClient.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/uno/XInterface.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <salhelper/thread.hxx>
#include <osl/socket.hxx>
#include <memory>
#include <functional>

#define IMPLEMENTATION_NAME "inco.niocs.test.SimpleStockClientImpl"

#define RINGBUFFERLEN 100
#define JSONBUFFERLEN 200*1024

namespace com
{
    namespace sun
    {
	namespace star
	{
	    namespace uno
	    {
		class XComponentContext;
	    }
	}
    }
}

typedef std::function<void(css::uno::Reference< css::sheet::XSpreadsheet >&, sal_Int32, sal_uInt32, double, sal_uInt32)> entry_proc;

void showEntryInSpreadSheet( css::uno::Reference< css::sheet::XSpreadsheet >& rxSheet, sal_Int32 nLinearIdx, sal_uInt32 nTstamp, double fPrice, sal_uInt32 nVolume );

struct StockRingBuffer
{
    sal_uInt32 aTstamp[RINGBUFFERLEN];
    double     aPrice[RINGBUFFERLEN];
    sal_uInt32 aVolume[RINGBUFFERLEN];
    sal_Int32 nBeg;
    sal_Int32 nEnd;
    StockRingBuffer() : nBeg(-1), nEnd(-1) {}
    void addEntry( sal_uInt32 nTstamp, double fPrice, sal_uInt32 nVolume );
    void runForEach( entry_proc aProcFunc, css::uno::Reference< css::sheet::XSpreadsheet >& rxSheet );
};

class StockClientConnection
{
    std::unique_ptr<osl::ConnectorSocket> pSocket;
    sal_Bool bConnected;
    char maJsonBuffer[JSONBUFFERLEN];
public:
    StockClientConnection();
    ~StockClientConnection();
    sal_Bool getData( StockRingBuffer& rRingBuf, sal_uInt32 nFrom );
    ::rtl::OUString getError();
    sal_Bool isConnected() { return bConnected; }
};

class StockClientWorkerThread : public salhelper::Thread
{
    css::uno::Reference< css::sheet::XSpreadsheet > mxSheet;
    sal_Bool mbEnableUpdates;
    sal_Bool mbStopWorker;
    StockClientConnection maConn;
    StockRingBuffer maBuf;
public:
    StockClientWorkerThread() : salhelper::Thread("StockClientWorker") {}
    void setSheet( const css::uno::Reference< css::sheet::XSpreadsheet >& rxSheet ) { mxSheet = rxSheet; }
    void setEnableUpdates( const sal_Bool bSet ) { mbEnableUpdates = bSet; }
    void stopWorker() { mbStopWorker = true; }
    virtual void SAL_CALL execute();
};

class SimpleStockClientImpl : public cppu::WeakImplHelper2
<
    css::lang::XServiceInfo,
    inco::niocs::test::XStockClient
>
{
    css::uno::Reference< css::sheet::XSpreadsheet > mxSheet;
    std::unique_ptr<StockClientWorkerThread> mpWorker;
public:
    SimpleStockClientImpl();
    virtual ~SimpleStockClientImpl();

    // XStockClient methods
    virtual void SAL_CALL setEnableUpdates( const sal_Bool bSet );
    virtual void SAL_CALL setSheet( const css::uno::Reference< css::sheet::XSpreadsheet >& rxSheet )
    {
        mpWorker->setSheet( rxSheet );
        mpWorker->launch();
    }
    
    // XServiceInfo methods
    virtual ::rtl::OUString SAL_CALL getImplementationName()
        throw (css::uno::RuntimeException);
    virtual sal_Bool SAL_CALL supportsService( const ::rtl::OUString& aServiceName )
        throw (css::uno::RuntimeException);
    virtual css::uno::Sequence< ::rtl::OUString > SAL_CALL getSupportedServiceNames()
        throw (css::uno::RuntimeException);
};


css::uno::Sequence< ::rtl::OUString > SAL_CALL SimpleStockClientImpl_getSupportedServiceNames()
    throw (css::uno::RuntimeException);

::com::sun::star::uno::Reference< ::com::sun::star::uno::XInterface > SAL_CALL SimpleStockClientImpl_createInstance(
    const css::uno::Reference< css::uno::XComponentContext > & rContext )
    throw( css::uno::Exception );

#endif
