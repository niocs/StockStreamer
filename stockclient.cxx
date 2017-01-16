
#include <stockclient.hxx>
#include <osl/time.h>
//#include <cstdint>
//#include <string.h>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <com/sun/star/table/XCell.hpp>
#include <cppuhelper/supportsservice.hxx>
#include <uno/lbnames.h>
#include <cppuhelper/factory.hxx>
#include <com/sun/star/lang/XSingleComponentFactory.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/registry/XRegistryKey.hpp>

#include <rapidjson/document.h>

using namespace ::rtl; // for OUString
using namespace ::com::sun::star; // for sdk interfaces
using namespace ::com::sun::star::uno; // for basic types
using namespace ::com::sun::star::sheet;
using namespace ::com::sun::star::table;
using namespace ::osl;
using namespace ::cppu;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::registry;

#define SERVICE_NAME "inco.niocs.test.StockClient"

void showEntryInSpreadSheet( css::uno::Reference< css::sheet::XSpreadsheet >& rxSheet,
                             sal_Int32 nLinearIdx,
                             sal_uInt32 nTstamp,
                             double fPrice,
                             sal_uInt32 nVolume )
{
    if ( !rxSheet.is() )
        return;
    TimeValue aTimeval;
    aTimeval.Seconds = nTstamp;
    oslDateTime aDateTime;
    osl_getDateTimeFromTimeValue( &aTimeval, &aDateTime );
    sal_Int32 nRow = 7 + nLinearIdx;
    Reference< XCell > xCell = rxSheet->getCellByPosition( 0, nRow );
    char tmbuf[100];
    sal_Int32 nLen = sprintf( tmbuf, "=TIME(%d; %d; %d)", aDateTime.Hours, aDateTime.Minutes, aDateTime.Seconds);
    xCell->setFormula( OUString(static_cast<sal_Char*>( tmbuf ), nLen, RTL_TEXTENCODING_ASCII_US) );
    xCell = rxSheet->getCellByPosition( 1, nRow );
    xCell->setValue( fPrice );
    xCell = rxSheet->getCellByPosition( 2, nRow );
    xCell->setValue( static_cast<double>( nVolume ) );
}

void StockRingBuffer::addEntry( sal_uInt32 nTstamp, double fPrice, sal_uInt32 nVolume )
{
    nEnd = ( ( nEnd + 1) % RINGBUFFERLEN );
    if ( nEnd == nBeg )
        nBeg = ( ( nBeg + 1) % RINGBUFFERLEN );
    if ( nBeg == -1 )  // Happens only for the first entry
        nBeg = 0;

    aTstamp[nEnd] = nTstamp;
    aPrice[nEnd]  = fPrice;
    aVolume[nEnd] = nVolume;
}

void StockRingBuffer::runForEach( entry_proc aProcFunc, css::uno::Reference< css::sheet::XSpreadsheet >& rxSheet )
{
    sal_Int32 nIdx       = ( ( nBeg + 1 ) % RINGBUFFERLEN );
    sal_Int32 nEndPlus1  = ( ( nEnd + 1 ) % RINGBUFFERLEN );
    sal_Int32 nLinearIdx = 0;
    aProcFunc( rxSheet, nLinearIdx, aTstamp[nBeg], aPrice[nBeg], aVolume[nBeg] );
    ++nLinearIdx;
    while ( nIdx != nEndPlus1 )
    {
        aProcFunc( rxSheet, nLinearIdx, aTstamp[nIdx], aPrice[nIdx], aVolume[nIdx] );
        nIdx = ( ( nIdx + 1 ) % RINGBUFFERLEN );
        ++nLinearIdx;
    }
}

StockClientConnection::StockClientConnection() :
    bConnected(false), pSocket(nullptr)
{}

StockClientConnection::~StockClientConnection()
{
    if ( bConnected )
        pSocket->close();
    printf("DEBUG>>> In StockClientConnection dtor\n");fflush(stdout);
}

OUString StockClientConnection::getError()
{
    if ( bConnected )
        return pSocket->getErrorAsString();
    return "";
}

sal_Bool StockClientConnection::getData( StockRingBuffer& rRingBuf, sal_uInt32 nFrom )
{
    if ( !bConnected )
    {
        SocketAddr aAddr("localhost", 1239);
        TimeValue aTimeout;
        aTimeout.Seconds = 1;
        printf( "DEBUG>>> Attempting to connect\n" );
        fflush( stdout );
        pSocket.reset( new ConnectorSocket );
        oslSocketResult aRes = pSocket->connect( aAddr, &aTimeout );
        if ( aRes != osl_Socket_Ok )
        {
            printf( "DEBUG>>> Connection attempt failed : errcode = %d\n", aRes );
            fflush( stdout );
            return false;
        }
        printf( "DEBUG>>> Connection attempt sucessfull\n" );
        fflush( stdout );
        bConnected = true;
    }
    char aInputJson[200];
    sal_Int32 nInputLength = sprintf( aInputJson, "{\"Ticker\":\"GOOG\",\"From\":%d}", nFrom );

    sal_Int32 nWritten = pSocket->write( aInputJson, nInputLength );
    if ( nWritten != nInputLength )
    {
        pSocket->close();
        bConnected = false;
        return false;
    }

    sal_Int32 nOutputLength = 0;
    if ( pSocket->read( &nOutputLength, 4 ) != 4 )
    {
        pSocket->close();
        bConnected = false;
        return false;
    }

    printf("DEBUG>>> Output length = %d\n", nOutputLength);fflush(stdout);
    sal_Int32 nRead = pSocket->read( maJsonBuffer, nOutputLength );
    if ( nRead != nOutputLength )
    {
        printf( "DEBUG>>> nRead = %d, but nOutputLength = %d. Closing conn.\n", nRead, nOutputLength );
        fflush(stdout);
        pSocket->close();
        bConnected = false;
        return false;
    }
    
    if ( nOutputLength < JSONBUFFERLEN )
    {
        maJsonBuffer[nOutputLength] = '\0';
        rapidjson::Document aDoc;
        if ( aDoc.Parse( maJsonBuffer ).HasParseError() )
        {
            printf( "DEBUG>>> Json Parse Error !\n" );
            fflush(stdout);
            pSocket->close();
            bConnected = false;
            return false;
        }
        const rapidjson::Value& aTstamp = aDoc["Tstamp"];
        const rapidjson::Value& aPrice  = aDoc["Price"];
        const rapidjson::Value& aVolume = aDoc["Volume"];
        sal_Int32 nDataLength = static_cast<sal_Int32>( aTstamp.Size() );
        if ( nDataLength <= 0 )
        {
            printf( "DEBUG>>> Server returned no data !\n" );
            fflush( stdout );
        }
        else
        {
            printf( "DEBUG>>> Server returned %d entries.\n", nDataLength );
            for ( sal_Int32 nIdx = 0; nIdx < nDataLength; ++nIdx )
                rRingBuf.addEntry( static_cast<sal_uInt32>( aTstamp[nIdx].GetInt() ),
                                   aPrice[nIdx].GetDouble(),
                                   static_cast<sal_uInt32>( aVolume[nIdx].GetInt() ) );
            printf( "DEBUG>>> nBeg = %d, nEnd = %d\n", rRingBuf.nBeg, rRingBuf.nEnd);
            fflush( stdout );
        }
    }
    else
    {
        printf("DEBUG>>> Json buffer overflow !\n");
        fflush(stdout);
    }

    return true;
}

void SAL_CALL StockClientWorkerThread::execute()
{
    if ( !mxSheet.is() )
    {
        printf("DEBUG>>> Bad sheet passed to StockClientWorkerThread\n");fflush(stdout);
        return;
    }

    mbStopWorker = false;
    TimeValue aTV;
    aTV.Seconds = 4;
    Reference< XCell > xCell = mxSheet->getCellByPosition(0, 6);
    xCell->setFormula( "Timestamp" );
    xCell = mxSheet->getCellByPosition(1, 6);
    xCell->setFormula( "Price" );
    xCell = mxSheet->getCellByPosition(2, 6);
    xCell->setFormula( "Volume" );
    Reference< XCell > xCellConnectionStatus = mxSheet->getCellByPosition(4, 6);
    Reference< XCell > xCellError = mxSheet->getCellByPosition(5, 6);
    while ( ! mbStopWorker )
    {
        if ( mbEnableUpdates )
        {
            sal_Bool bOK = false;
            sal_uInt32 nFrom = 0;
            if ( maBuf.nEnd == -1 )
            {
                TimeValue aTimeval;
                osl_getSystemTime( &aTimeval );
                nFrom = aTimeval.Seconds - 4;
            }
            else
                nFrom = maBuf.aTstamp[maBuf.nEnd] + 1;

            bOK = maConn.getData( maBuf, nFrom );
            xCellConnectionStatus->setFormula( maConn.isConnected() ? OUString("Connected") : OUString("Not Connected") );
            maBuf.runForEach( showEntryInSpreadSheet, mxSheet );
            xCellError->setFormula( bOK ? OUString("No Error") : maConn.getError() );
        }
        salhelper::Thread::wait( aTV );
    }
}

SimpleStockClientImpl::SimpleStockClientImpl() : mpWorker( new StockClientWorkerThread() )
{
    mpWorker->setEnableUpdates( false );
    printf("DEBUG>>> SimpleStockClientImpl ctor : %p\n", this);fflush(stdout);
}

SimpleStockClientImpl::~SimpleStockClientImpl()
{
    if ( mpWorker.get() )
        mpWorker->stopWorker();
    printf("DEBUG>>> SimpleStockClientImpl dtor : %p\n", this);fflush(stdout);
}

void SAL_CALL SimpleStockClientImpl::setEnableUpdates( const sal_Bool bSet )
{
    if ( mpWorker.get() )
        mpWorker->setEnableUpdates( bSet );
}

OUString SAL_CALL SimpleStockClientImpl::getImplementationName()
    throw (RuntimeException)
{
    return IMPLEMENTATION_NAME;
}

sal_Bool SAL_CALL SimpleStockClientImpl::supportsService( const OUString& rServiceName )
    throw (RuntimeException)
{
    return cppu::supportsService(this, rServiceName);
}

Sequence< OUString > SAL_CALL SimpleStockClientImpl::getSupportedServiceNames()
    throw (RuntimeException)
{
    return SimpleStockClientImpl_getSupportedServiceNames();
}


struct Instance {
    explicit Instance(
	Reference<XComponentContext> const & /*rxContext*/):
	instance(
	    static_cast<cppu::OWeakObject *>(new SimpleStockClientImpl()))
    {}
    Reference<XInterface> instance;
};


struct Singleton:
    public rtl::StaticWithArg<
    Instance, Reference<XComponentContext>, Singleton>
{};


Sequence< OUString > SAL_CALL SimpleStockClientImpl_getSupportedServiceNames()
    throw (RuntimeException)
{
    Sequence < OUString > aRet(1);
    OUString* pArray = aRet.getArray();
    pArray[0] =  OUString ( SERVICE_NAME );
    return aRet;    
}

Reference< XInterface > SAL_CALL SimpleStockClientImpl_createInstance( const Reference< XComponentContext > & rxContext )
    throw( Exception )
{
    return cppu::acquire(static_cast<cppu::OWeakObject *>(
			     Singleton::get(rxContext).instance.get()));
}

extern "C" SAL_DLLPUBLIC_EXPORT void * SAL_CALL component_getFactory(const sal_Char * pImplName, void * /*pServiceManager*/, void * pRegistryKey)
{
    void * pRet = 0;

    if (rtl_str_compare( pImplName, IMPLEMENTATION_NAME ) == 0)
    {
        Reference< XSingleComponentFactory > xFactory( createSingleComponentFactory(
            SimpleStockClientImpl_createInstance,
            OUString( IMPLEMENTATION_NAME ),
            SimpleStockClientImpl_getSupportedServiceNames() ) );

        if (xFactory.is())
        {
            xFactory->acquire();
            pRet = xFactory.get();
        }
    }

    return pRet;
}

extern "C" SAL_DLLPUBLIC_EXPORT void SAL_CALL
component_getImplementationEnvironment(
    char const ** ppEnvTypeName, uno_Environment **)
{
    *ppEnvTypeName = CPPU_CURRENT_LANGUAGE_BINDING_NAME;
}
