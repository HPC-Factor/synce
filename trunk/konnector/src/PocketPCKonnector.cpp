/***************************************************************************
*   Copyright (C) 2004 by Christian Fremgen                               *
*   cfremgen@users.sourceforge.net                                        *
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
*   This program is distributed in the hope that it will be useful,       *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*   GNU General Public License for more details.                          *
*                                                                         *
*   You should have received a copy of the GNU General Public License     *
*   along with this program; if not, write to the                         *
*   Free Software Foundation, Inc.,                                       *
*   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
***************************************************************************/
#include "PocketPCKonnector.h"
#include "PocketPCKonnectorConfig.h"
#include "PDAIdHelper.h"


//#include "CalendarHandler.h"
#include "AddressBookHandler.h"

#include "todohandler.h"
#include "eventhandler.h"

#include <qdir.h>
#include <kstandarddirs.h>
#include <ktempfile.h>

#include <kabc/resourcefile.h>
#include "resourcenull.h"
#include <kabc/vcardconverter.h>

#include <libkcal/icalformat.h>

//#define __USE_ABOVE_KDEPIM_3_3_0__
//#ifdef __USE_ABOVE_KDEPIM_3_3_0__
#include <kitchensync/addressbooksyncee.h>
#include <kitchensync/calendarsyncee.h>
#include <kitchensync/idhelper.h>
#include <kitchensync/konnectormanager.h>
/*
#else
#include <addressbooksyncee.h>
#include <calendarsyncee.h>
#include <idhelper.h>
#endif
*/


//namespace KSync {

class PocketPCKonnectorFactory : public KRES::PluginFactoryBase
{
public:
    KRES::Resource* resource ( const KConfig* p_config )
    {
        return new KSync::PocketPCKonnector( p_config );
    }

    KRES::ConfigWidget* configWidget ( QWidget* p_parent )
    {
        return new pocketPCHelper::PocketPCKonnectorConfig( p_parent, "PocketPCKonnectorConfig" );
    }
};


extern "C"
{
    void* init_libsyncekonnector() {
        return new PocketPCKonnectorFactory();
    }
}


namespace KSync
{

    PocketPCKonnector::PocketPCKonnector( const KConfig* p_config )
            : KSync::Konnector( p_config )
    {
        if ( p_config ) {
            m_pdaName = p_config->readEntry( "PDAName" );
        }

        m_rraExists = false;
        m_uidHelper = 0;

        m_addressBook = new KABC::AddressBook();
        m_addressBook->addResource ( new pocketPCCommunication::ResourceNull() );

        m_calendar = new KCal::CalendarLocal();

        //#ifdef __USE_ABOVE_KDEPIM_3_3_0__
        kdDebug( 2120 ) << "PocketPCKonnector::storagePath: " << storagePath() << endl;

        m_baseDir = storagePath() + "/pocketpc/";
    }


    PocketPCKonnector::~PocketPCKonnector()
    {
        kdDebug( 2120 ) << "PocketPCKonnector::~PocketPCKonnector" << endl;

        if ( m_rraExists )
            kdDebug( 2120 ) << "PocketPCKonnector::~ PocketPCKonnector: m_rraExists is true" << endl;

        if ( m_rraExists ) {
            if ( m_rra.data() ) {
                kdDebug( 2120 ) << "PocketPCKonnector::~PocketPCKonnector: before m_rra->finalDisconnect()" << endl;
                m_rra->finalDisconnect();
            }
        }
        if ( m_rraExists )
            kdDebug( 2120 ) << "PocketPCKonnector::~PocketPCKonnector: m_rra.count(): " << m_rra.count() << endl;

        delete m_addressBook;
        delete m_calendar;
    }


    SynceeList PocketPCKonnector::syncees()
    {
        kdDebug( 2120 ) << "PocketPCKonnector::syncees() returning syncees" << endl;
        return m_syncees;
    }


    bool PocketPCKonnector::readSyncees()
    {
        // clear the internal structures;
        clearDataStructures();

        if ( !m_rra.data() )  // can not be connected!!!
        {
            emit synceeReadError( this );
            return false;
        }
/*
        if ( !m_rra->isConnected() )  // not connected!!
        {
            emit synceeReadError( this );
            return false;
        }
*/

        if ( !m_rra->connect() ) {
            kdDebug( 2120 ) << "PocketPCKonnector: could not connect to device!" << endl;
            return false;
        } else {
            kdDebug( 2120 ) << "PocketPCKonnector: connected to device!" << endl;;
        }

        m_syncees.clear();

        pocketPCCommunication::AddressBookHandler addrHandler( m_rra );
//        pocketPCCommunication::CalendarHandler calHandler( m_rra );
        pocketPCCommunication::EventHandler eventHandler(m_rra);
        pocketPCCommunication::TodoHandler todoHandler(m_rra);

        QString base;
        base = m_baseDir + eventHandler.getPartnerId(); //QDir::homeDirPath() + "/.kitchensync/meta/pocketpc/" + eventHandler.getPartnerId();

        bool firstSync = false;

        if ( !m_uidHelper ) {
            QDir dir;
            QString dirName = base; // check that dir does not exist!!!!!!!
            if ( !dir.exists( dirName ) ) {
                // do something about firstSync!
                firstSync = true;
                dir.mkdir ( dirName );
            }
            m_uidHelper = new KSync::KonnectorUIDHelper ( dirName );
        }

        KSync::AddressBookSyncee* addrSyncee = 0;
        KSync::CalendarSyncee* calSyncee = 0;


        if ( firstSync ) {
            kdDebug( 2120 ) << "PocketPCKonnector: FirstSync!!!!" << endl;
            // get the complete addressBook.. because it is the first sync
            if ( !addrHandler.getAddressBook( *m_addressBook, pocketPCCommunication::ALL ) ) {
                emit synceeReadError( this );
                return false;
            }


            // try to make a final disconnect to allow further subsription of events
            m_rra->disconnect();
            m_rra->connect();

            // do just the same for getting the complete calendar (events and todos are separated)
            if ( !eventHandler.getAllEvents( *m_calendar, pocketPCCommunication::ALL ) ) {
                emit synceeReadError( this );
                return false;
            }

            m_rra->disconnect(); // think this is necessary.. *hmpf*
            m_rra->connect();

            if ( !todoHandler.getAllTodos( *m_calendar, pocketPCCommunication::ALL ) ) {
                emit synceeReadError( this );
                return false;
            }

            addrSyncee = new KSync::AddressBookSyncee( m_addressBook );
            calSyncee = new KSync::CalendarSyncee( m_calendar );

            firstSyncModifications( addrSyncee );
            firstSyncModifications( calSyncee );

            m_syncees.append ( addrSyncee );
            m_syncees.append ( calSyncee );
        } else {
            // well.. aeh.. read the ids from the id-files...

            PDAIdHelper pdaAddressbookIdHelper( base );

            addrSyncee = new KSync::AddressBookSyncee ( m_addressBook );
            m_syncees.append ( addrSyncee );

            loadAddressbookMetaData( m_baseDir + addrHandler.getPartnerId() );

            QStringList adrIds;
            pdaAddressbookIdHelper.readPDAIds ( "AddressBook.ids", adrIds );

            kdDebug( 2120 ) << "dumping pdaIdHelper-ids:" << endl;
            pdaAddressbookIdHelper.dumpPDAIds( adrIds );

            QMap<QString, pocketPCCommunication::RecordType> addrStatusMap;
            if ( !addrHandler.getIdStatus( addrStatusMap ) ) {
                emit synceeReadError( this );
                return false;
            }

            QStringList remoteAdrIds = pdaAddressbookIdHelper.createIdQStringList( addrStatusMap );
            QStringList addedAdrIds = pdaAddressbookIdHelper.getPDAAddedIds( remoteAdrIds, adrIds );
            QStringList removedAdrIds = pdaAddressbookIdHelper.getPDARemovedIds( remoteAdrIds, adrIds );
            QStringList modifiedAdrIds = pdaAddressbookIdHelper.getPDAModifiedIds( addrStatusMap, addedAdrIds );

            kdDebug( 2120 ) << "Dumping Ids for AddressBookSyncee: " << endl;
            kdDebug( 2120 ) << "all ids: " << endl;
            pdaAddressbookIdHelper.dumpPDAIds( remoteAdrIds );
            kdDebug( 2120 ) << "addedIds: " << endl;
            pdaAddressbookIdHelper.dumpPDAIds( addedAdrIds );
            kdDebug( 2120 ) << "removedIds: " << endl;
            pdaAddressbookIdHelper.dumpPDAIds( removedAdrIds );
            kdDebug( 2120 ) << "modifiedIds: " << endl;
            pdaAddressbookIdHelper.dumpPDAIds( modifiedAdrIds );

            KABC::Addressee::List addedAdrList;
            addrHandler.getAddressees( addedAdrList, addedAdrIds );

            KABC::Addressee::List modifiedAdrList;
            addrHandler.getAddressees ( modifiedAdrList, modifiedAdrIds );

            kdDebug( 2120 ) << "PocketPCKonnector: updating AddressBook Syncee" << endl;
            updateAddressBookSyncee( addedAdrList, modifiedAdrList, removedAdrIds );

            pdaAddressbookIdHelper.savePDAIds("AddressBook.ids", addrStatusMap);

            {
                // and now... setSyncEntry on all Undefined entries in the syncee!!
                // this is just to get the correct local ids!!
                KSync::SyncEntry* entry = addrSyncee->firstEntry();
                while ( entry ) {
                    if ( entry->state() == KSync::SyncEntry::Undefined )
                        setSyncEntry ( entry, KSync::SyncEntry::Undefined );
                    entry = addrSyncee->nextEntry();
                }
                kdDebug( 2120 ) << "PocketPCKonnector::updateAddressBookSyncee end" << endl;
            }

            dumpIds( addrSyncee );

            m_rra->disconnect();
            m_rra->connect();










            calSyncee = new KSync::CalendarSyncee( m_calendar );
            m_syncees.append ( calSyncee );

            loadCalendarMetaData( m_baseDir + eventHandler.getPartnerId() );

            PDAIdHelper pdaEventIdHelper( base );

            QStringList eventIds;
            pdaEventIdHelper.readPDAIds ( "Event.ids", eventIds );

            kdDebug( 2120 ) << "dumping pdaIdHelper-ids:" << endl;
            pdaEventIdHelper.dumpPDAIds( eventIds );

            QMap<QString, pocketPCCommunication::RecordType> eventStatusMap;

            if ( !eventHandler.getIdStatus( eventStatusMap ) ) {
                emit synceeReadError( this );
                return false;
            }

            QStringList remoteEventIds = pdaEventIdHelper.createIdQStringList( eventStatusMap );
            QStringList addedEventIds = pdaEventIdHelper.getPDAAddedIds( remoteEventIds, eventIds );
            QStringList removedEventIds = pdaEventIdHelper.getPDARemovedIds( remoteEventIds, eventIds );
            QStringList modifiedEventIds = pdaEventIdHelper.getPDAModifiedIds( eventStatusMap, addedEventIds );

            kdDebug( 2120 ) << "Dumping Ids for CalendarSyncee: " << endl;
            kdDebug( 2120 ) << "all ids: " << endl;
            pdaEventIdHelper.dumpPDAIds( remoteEventIds );
            kdDebug( 2120 ) << "addedIds: " << endl;
            pdaEventIdHelper.dumpPDAIds( addedEventIds );
            kdDebug( 2120 ) << "removedIds: " << endl;
            pdaEventIdHelper.dumpPDAIds( removedEventIds );
            kdDebug( 2120 ) << "modifiedIds: " << endl;
            pdaEventIdHelper.dumpPDAIds( modifiedEventIds );

            m_rra->disconnect();
            m_rra->connect();

            KCal::Event::List addedEventList;
            eventHandler.getEvents( addedEventList, addedEventIds );

            KCal::Event::List modifiedEventList;
            eventHandler.getEvents( modifiedEventList, modifiedEventIds );

            updateCalendarSyncee(addedEventList, modifiedEventList, removedEventIds);

            pdaEventIdHelper.savePDAIds("Event.ids", eventStatusMap);








            m_rra->disconnect();
            m_rra->connect();

            PDAIdHelper pdaTodoIdHelper( base );

            QStringList todoIds;
            pdaTodoIdHelper.readPDAIds ( "Todo.ids", todoIds );

            kdDebug( 2120 ) << "dumping pdaIdHelper-ids:" << endl;
            pdaTodoIdHelper.dumpPDAIds( todoIds );

            QMap<QString, pocketPCCommunication::RecordType> todoStatusMap;

            if ( !todoHandler.getIdStatus( todoStatusMap ) ) {
                emit synceeReadError( this );
                return false;
            }

            QStringList remoteTodoIds = pdaTodoIdHelper.createIdQStringList( todoStatusMap );
            QStringList addedTodoIds = pdaTodoIdHelper.getPDAAddedIds( remoteTodoIds, todoIds );
            QStringList removedTodoIds = pdaTodoIdHelper.getPDARemovedIds( remoteTodoIds, todoIds );
            QStringList modifiedTodoIds = pdaTodoIdHelper.getPDAModifiedIds( todoStatusMap, addedTodoIds );

            kdDebug( 2120 ) << "Dumping Ids for CalendarSyncee: " << endl;
            kdDebug( 2120 ) << "all ids: " << endl;
            pdaTodoIdHelper.dumpPDAIds( remoteTodoIds );
            kdDebug( 2120 ) << "addedIds: " << endl;
            pdaTodoIdHelper.dumpPDAIds( addedTodoIds );
            kdDebug( 2120 ) << "removedIds: " << endl;
            pdaTodoIdHelper.dumpPDAIds( removedTodoIds );
            kdDebug( 2120 ) << "modifiedIds: " << endl;
            pdaTodoIdHelper.dumpPDAIds( modifiedTodoIds );

            m_rra->disconnect();
            m_rra->connect();

            KCal::Todo::List addedTodoList;
            todoHandler.getTodos( addedTodoList, addedTodoIds );

            KCal::Todo::List modifiedTodoList;
            todoHandler.getTodos( modifiedTodoList, modifiedTodoIds );

            updateCalendarSyncee(addedTodoList, modifiedTodoList, removedTodoIds);

            pdaTodoIdHelper.savePDAIds("Todo.ids", todoStatusMap);

            {
                // and now... setSyncEntry on all Undefined entries in the syncee!!
                // this is just to get the corret local ids!!
                KSync::SyncEntry* entry = calSyncee->firstEntry();
                while ( entry ) {
                    if ( entry->state() == KSync::SyncEntry::Undefined ) {
                        setSyncEntry ( entry, KSync::SyncEntry::Undefined );
                    }
                    entry = calSyncee->nextEntry();
                }
            }

            dumpIds( calSyncee );
        }

        addrSyncee->setIdentifier ( "addrBook-device" );
        addrSyncee->setTitle ( "device" );

        calSyncee->setIdentifier ( "calendar-device" );
        calSyncee->setTitle ( "device" );

        m_rra->disconnect();

        emit synceesRead ( this );

        return true;
    }


    bool PocketPCKonnector::writeSyncees()
    {
        // well.. do some debugging here.... show the original ids of the entries...
        // and check what syncee->ids contains

        if ( m_syncees.empty() ) {
            kdDebug( 2120 ) << "PocketPCKonnector::writeSyncees: m_syncees is empty" << endl;
            return false;
        }

        m_rra->connect();

        kdDebug( 2120 ) << "PocketPCKonnector:: m_syncees.addressBookSyncee() read" << endl;


        KSync::AddressBookSyncee* addrSyncee = m_syncees.addressBookSyncee();
        KSync::CalendarSyncee* calSyncee = m_syncees.calendarSyncee();

        dumpIds ( addrSyncee );
        dumpIds ( calSyncee );

        if ( m_uidHelper ) {
            saveIds ( addrSyncee, "AddressBookSyncEntry" );
            saveIds ( calSyncee, "CalendarSyncEntry" );

            kdDebug( 2120 ) << "Saving KonnectorUIDHelper within PocketPCKonnector" << endl;
            m_uidHelper->save();
        }


        // and now... the entries within the syncees need their konnector-id back.. (but without the trailing konnector-)
        // and: new ids which are created when pushing new entries to the pda need attention!!
        // eg: PimHandler can return a structure with all the necessary information.. this needs to be merged to the
        // KonnectorUIDHelper

        if ( m_uidHelper ) {
            kdDebug( 2120 ) << "Reassining ids for AddressBookSyncEntries:" << endl;
            // iterate through the structures and write the ids back..
            setKonnectorId ( addrSyncee, "AddressBookSyncEntry" );

            kdDebug( 2120 ) << "Reassining ids for CalendarSyncEntries:" << endl;
            setKonnectorId ( calSyncee, "CalendarSyncEntry" );
        }

        kdDebug( 2120 ) << "PocketPCKonnector::writeSyncees..." << endl;
        // write m_syncees to the device
        if ( m_syncees.empty() ) {
            kdDebug( 2120 ) << "PocketPCKonnector::writeSyncees: m_syncees is empty" << endl;
            emit synceeWriteError( this );
            m_rra->disconnect();
            return false;
        }

        if ( !m_rra.data() )  // can not be connected!!!
        {
            kdDebug( 2120 ) << "PocketPCKonnector::writeSyncees: m_rra.data() is 0" << endl;
            emit synceeWriteError( this );
            m_rra->disconnect();
            return false;
        }

        if ( !m_rra->isConnected() )  // not connected!!
        {
            kdDebug( 2120 ) << "PocketPCKonnector::writeSyncees: m_rra->isConnected is false" << endl;
            emit synceeWriteError( this );
            m_rra->disconnect();
            return false;
        }

        QString partnerId;

        if ( addrSyncee->isValid() ) {
            KABC::Addressee::List addrAdded;
            KABC::Addressee::List addrRemoved;
            KABC::Addressee::List addrModified;

            getAddressees( addrAdded, addrSyncee->added() );
            getAddressees( addrRemoved, addrSyncee->removed() );
            getAddressees( addrModified, addrSyncee->modified() );

            pocketPCCommunication::AddressBookHandler addrHandler( m_rra );

            addrHandler.addAddressees( addrAdded );
            addrHandler.removeAddressees( addrRemoved );
            addrHandler.updateAddressees( addrModified );

            if ( m_uidHelper ) {
                QStringList appIds = addNewIds( addrSyncee, "AddressBookSyncEntry", addrHandler.getIdPairs() );
                QString base;
                base = m_baseDir + addrHandler.getPartnerId(); //QDir::homeDirPath() + "/.kitchensync/meta/pocketpc/" + calHandler.getPartnerId();
                PDAIdHelper pdaIdHelper ( base );
                pdaIdHelper.appendPDAIds ( "AddressBook.ids", appIds );

                removeOldIds ( "AddressBookSyncEntry", addrSyncee->removed() );

            }

            partnerId = addrHandler.getPartnerId();

            if ( !partnerId.isEmpty() )
                saveAddressbookMetaData( m_baseDir + partnerId );
        }


        if ( calSyncee->isValid() ) {
            KCal::Event::List eventsAdded;
            KCal::Event::List eventsRemoved;
            KCal::Event::List eventsModified;

            KCal::Todo::List todosAdded;
            KCal::Todo::List todosRemoved;
            KCal::Todo::List todosModified;

            getEvents( eventsAdded, todosAdded, calSyncee->added() );
            getEvents( eventsRemoved, todosRemoved, calSyncee->removed() );
            getEvents( eventsModified, todosModified, calSyncee->modified() );

            pocketPCCommunication::EventHandler eventHandler( m_rra );

            kdDebug( 2120 ) << "PocketPCKonnector::writeSyncees: writing events to pda" << endl;
            kdDebug( 2120 ) << "    adding events: " << eventsAdded.size() << endl;
            eventHandler.addEvents( eventsAdded );
            kdDebug( 2120 ) << "    removing events" << endl;
            eventHandler.removeEvents( eventsRemoved );
            kdDebug( 2120 ) << "    modifieing events" << endl;
            eventHandler.updateEvents( eventsModified );

            pocketPCCommunication::TodoHandler todoHandler( m_rra );

            kdDebug( 2120 ) << "PocketPCKonnector::writeSyncees: writing todos to pda" << endl;
            kdDebug( 2120 ) << "    adding todos: " << todosAdded.size() << endl;
            todoHandler.addTodos( todosAdded );
            kdDebug( 2120 ) << "    removing todos" << endl;
            todoHandler.removeTodos( todosRemoved );
            kdDebug( 2120 ) << "    modifieing todos" << endl;
            todoHandler.updateTodos( todosModified );

            if ( m_uidHelper ) {
                QStringList appEventIds = addNewIds( calSyncee, "CalendarSyncEntry", eventHandler.getIdPairs() );
                QString eventBase = m_baseDir + eventHandler.getPartnerId();
                PDAIdHelper pdaEventIdHelper ( eventBase );
                pdaEventIdHelper.appendPDAIds ( "Event.ids", appEventIds );


                QStringList appTodoIds = addNewIds( calSyncee, "CalendarSyncEntry", todoHandler.getIdPairs() );
                QString todoBase = m_baseDir + todoHandler.getPartnerId();
                PDAIdHelper pdaTodoIdHelper ( todoBase );
                pdaTodoIdHelper.appendPDAIds ( "Todo.ids", appTodoIds );

                removeOldIds( "CalendarSyncEntry", calSyncee->removed() );
            }

            partnerId = eventHandler.getPartnerId();

            if ( !partnerId.isEmpty() )
                saveCalendarMetaData( m_baseDir + partnerId );
        }



        if ( m_uidHelper )
            m_uidHelper->save();

        emit synceesWritten ( this );

        m_rra->disconnect();

        return true;
    }


    bool PocketPCKonnector::connectDevice()
    {
        if ( m_pdaName.isEmpty() )
            return false;

        m_rra = new pocketPCCommunication::Rra( m_pdaName );
        m_rra->setLogLevel( 0 );
/*
        if ( !m_rra->connect() ) {
            kdDebug( 2120 ) << "PocketPCKonnector: could not connect to device!" << endl;
            return false;
        } else {
            kdDebug( 2120 ) << "PocketPCKonnector: connected to device!" << endl;
            return true;
        }
*/
        m_rraExists = true;
        //return m_rra->connect();

        return true;
    }


    bool PocketPCKonnector::disconnectDevice()
    {
        if ( !m_rra.data() )
            return false;

//        m_rra->disconnect();
        return true;
    }


    KonnectorInfo PocketPCKonnector::info() const
    {
        if ( !m_rra.data() ) {
            return KonnectorInfo ( QString ( "PocketPC (WinCE) Konnector" ),
                                   QIconSet(),  //iconSet(),
                                   QString ( "WinCE 3.0 up" ),
                                   /*                              "0", //metaId(),
                                                                 "", //iconName(),*/
                                   false );
        } else {
            return KonnectorInfo ( QString ( "PocketPC (WinCE) Konnector" ),
                                   QIconSet(),  //iconSet(),
                                   QString ( "WinCE 3.0 up" ),
                                   /*                              "0", //metaId(),
                                                                 "", //iconName(),*/
                                   m_rra->isConnected() );
        }

    }


    void PocketPCKonnector::writeConfig( KConfig* p_config )
    {
        Konnector::writeConfig ( p_config );

        p_config->writeEntry ( "PDAName", m_pdaName );
    }


    void PocketPCKonnector::setPdaName ( const QString& p_pdaName )
    {
        m_pdaName = p_pdaName;
    }


    const QString PocketPCKonnector::getPdaName () const
    {
        return m_pdaName;
    }


    void PocketPCKonnector::firstSyncModifications ( KSync::Syncee* p_syncee )
    {
        KSync::SyncEntry * syncEntry = p_syncee->firstEntry();
        while ( syncEntry ) {
            //syncEntry->setState (KSync::SyncEntry::Added);
            setSyncEntry ( syncEntry, KSync::SyncEntry::Added );
            syncEntry = p_syncee->nextEntry();
        }
    }


    void PocketPCKonnector::setSyncEntry ( KSync::SyncEntry* p_entry, KSync::SyncEntry::Status p_status )
    {
        kdDebug( 2120 ) << "PocketPCKonnector::setSyncEntry" << endl;

        if ( !p_entry ) {
            kdDebug() << "     entry seems already to be changed, because I cannot find it!" << endl;
            return ;
        }

        p_entry->setState( p_status );

        // well.. set the id to Konnector-whatever-follows
        // later: check if we already have a local id!!!
        if ( m_uidHelper ) {
            kdDebug( 2120 ) << "setSyncEntry: the proof that m_uidHelper exists" << endl;
            QString newId = m_uidHelper->kdeId( p_entry->type(), "Konnector-" + p_entry->id(), "Konnector-" + p_entry->id() );
            kdDebug( 2120 ) << "changing id from " << p_entry->id() << " to " << newId << endl;
            p_entry->setId ( newId );
        } else
            p_entry->setId ( "Konnector-" + p_entry->id() );
    }


    void PocketPCKonnector::setKonnectorId ( KSync::Syncee* p_syncee, const QString& p_name )
    {
        KSync::SyncEntry * entry = p_syncee->firstEntry();
        while ( entry ) {
            kdDebug( 2120 ) << "    old id: " << entry->id() << "    " << endl;
            QString id = m_uidHelper->konnectorId ( p_name, entry->id() );
            if ( id.startsWith ( "Konnector-" ) )  // only in this case we REALLY have an id stored!!!
            {
                id = id.remove ( "Konnector-" );
                entry->setId ( id );
            }
            kdDebug( 2120 ) << "    new id: " << entry->id() << endl;
            entry = p_syncee->nextEntry();
        }
    }


    void PocketPCKonnector::saveIds ( KSync::Syncee* p_syncee, const QString& p_name )
    {
        Kontainer::ValueList newIds = p_syncee->ids( p_name );
        //#ifdef __USE_ABOVE_KDEPIM_3_3_0__
        for ( Kontainer::ValueList::ConstIterator idIt = newIds.begin(); idIt != newIds.end(); ++idIt ) {
            m_uidHelper->addId( p_name, ( *idIt ).first, ( *idIt ).second ); // FIXME update this name later
        }
        /*
        #else
        for ( Kontainer::ValueList::Iterator idIt = newIds.begin(); idIt != newIds.end(); ++idIt ) {
            m_uidHelper->addId(p_name,  (*idIt).first(),  (*idIt).second() ); // FIXME update this name later
        }
        #endif
        */

    }


    void PocketPCKonnector::dumpIds ( KSync::Syncee* p_syncee )
    {
        if ( p_syncee->isValid() ) {
            KSync::SyncEntry * entry = p_syncee->firstEntry();
            while ( entry ) {
                kdDebug( 2120 ) << "PocketPCKonnector:: current id: " << entry->id() << endl;
                kdDebug( 2120 ) << "PocketPCKonnector:: status: " << entry->state() << endl;
                entry = p_syncee->nextEntry();
            }

            QMap<QString, Kontainer::ValueList> idMap = p_syncee->ids();
            QMap<QString, Kontainer::ValueList>::iterator it = idMap.begin();
            for ( ; it != idMap.end(); ++it ) {
                kdDebug( 2120 ) << "   TypeName: " << it.key() << endl;
                Kontainer::ValueList valList = it.data();
                Kontainer::ValueList::iterator valIt = valList.begin();
                for ( ; valIt != valList.end(); ++valIt ) {
                    //#ifdef __USE_ABOVE_KDEPIM_3_3_0__
                    kdDebug( 2120 ) << "      ids in pair: " << ( *valIt ).first << "    " << ( *valIt ).second << endl;
                    /*
                    #else
                    kdDebug(2120) << "      ids in pair: " << (*valIt).first() << "    " << (*valIt).second() << endl;
                    #endif
                    */
                }
            }
        }
    }


    QStringList PocketPCKonnector::addNewIds ( KSync::Syncee* p_syncee, const QString& p_name, QValueList<QPair<QString, QString> > p_newIds )
    {
        QStringList newRraIds;
        QValueList<QPair<QString, QString> >::iterator it = p_newIds.begin();
        for ( ; it != p_newIds.end(); ++it ) {
            kdDebug( 2120 ) << "new id for " << p_name << ": " << ( *it ).first << "  " << ( *it ).second << endl;
            m_uidHelper->addId ( p_name, "Konnector-" + ( *it ).first, ( *it ).second );
            // and now.. find the id in this syncee and change the entry to the pda-id
            p_syncee->findEntry ( ( *it ).second ) ->setId ( ( *it ).first );
            newRraIds.push_back ( ( *it ).first );
        }
        return newRraIds;
    }


    void PocketPCKonnector::removeOldIds ( const QString& p_name, KSync::SyncEntry::PtrList p_oldIds )
    {
        kdDebug(2120) << "Removing old ids" << endl;
        KSync::SyncEntry::PtrList::iterator it = p_oldIds.begin();
        for ( ; it != p_oldIds.end(); ++it ) {
            kdDebug(2120) << "    ID: " << ( *it ) ->id() << endl;
            m_uidHelper->removeId ( p_name, "Konnector-" + ( *it ) ->id() );
        }
    }


    void PocketPCKonnector::clearDataStructures()
    {
        m_addressBook->clear();
        m_calendar->deleteAllEvents();
        m_calendar->deleteAllTodos();
        m_calendar->deleteAllJournals();

        m_syncees.clear();
    }


    void PocketPCKonnector::getAddressees ( KABC::Addressee::List& p_addressees, KSync::SyncEntry::PtrList p_ptrList )
    {
        kdDebug( 2120 ) << "getAddressees: " << endl;
        KSync::SyncEntry::PtrList::Iterator it = p_ptrList.begin();
        for ( ; it != p_ptrList.end(); ++it ) {
            p_addressees.push_back ( ( dynamic_cast<KSync::AddressBookSyncEntry*>( *it ) ) ->addressee() );
            kdDebug( 2120 ) << "     " << ( dynamic_cast<KSync::AddressBookSyncEntry*>( *it ) ) ->id() << endl;
        }
    }


    void PocketPCKonnector::getEvents ( KCal::Event::List& p_events, KCal::Todo::List& p_todos, KSync::SyncEntry::PtrList p_ptrList )
    {
        KSync::SyncEntry::PtrList::Iterator it = p_ptrList.begin();
        for ( ; it != p_ptrList.end(); ++it ) {
            kdDebug( 2120 ) << "PocketPCKonnector::getEvents type of entry: " << ( dynamic_cast<KSync::CalendarSyncEntry*>( *it ) ) ->incidence() ->type() << endl;
            QString type = ( dynamic_cast<KSync::CalendarSyncEntry*>( *it ) ) ->incidence() ->type();
            if ( type == "Todo" )
                p_todos.push_back ( dynamic_cast<KCal::Todo*>( ( dynamic_cast<KSync::CalendarSyncEntry*>( *it ) ) ->incidence() ) );
            else if ( type == "Event" )
                p_events.push_back ( dynamic_cast<KCal::Event*>( ( dynamic_cast<KSync::CalendarSyncEntry*>( *it ) ) ->incidence() ) );
        }
    }


    void PocketPCKonnector::loadAddressbookMetaData ( const QString& p_dir )
    {
        // read directly into the syncees!!! syncee has to be empty
        // so the syncees have to exist at this point!!
        // and they do that now :)

        QString addrFileName = p_dir + "/AddressBookMeta.vcf";

        kdDebug( 2120 ) << "loadMetaData: " << addrFileName << endl;
        // load the addressBook
        QFile addrFile( addrFileName );
        if ( addrFile.open ( IO_ReadOnly ) ) {
            QTextStream addrStream( &addrFile );
            KABC::VCardConverter conv;

            QString vcards;
            vcards = addrStream.read();

            KABC::Addressee::List addrList = conv.parseVCards ( vcards );

            KABC::Addressee::List::Iterator it = addrList.begin();

            KSync::AddressBookSyncee* addrSyncee = m_syncees.addressBookSyncee();
            for ( ; it != addrList.end(); ++it ) {
                addrSyncee->addEntry ( new KSync::AddressBookSyncEntry ( ( *it ), addrSyncee ) );
            }

            addrFile.close();
        }
    }


    void PocketPCKonnector::loadCalendarMetaData ( const QString& p_dir )
    {
        QString calFileName = p_dir + "/CalendarMeta.ics";

        kdDebug( 2120 ) << "loadMetaData: " << calFileName << endl;
        QFile calFile( calFileName );
        if ( calFile.open ( IO_ReadOnly ) ) {
            QTextStream calStream( &calFile );
            KCal::ICalFormat conv;

            QString ical;
            ical = calStream.read();

            KCal::CalendarLocal cal;
            conv.fromString ( &cal, ical );


            KSync::CalendarSyncee* calSyncee = m_syncees.calendarSyncee();

            // and now.. iterate over the calendar, and fill the syncee...
            // or do it directly? could be a problem with dynamic memory... also the other way round.. *hmpf*

            KCal::Event::List events = cal.events();
            KCal::Event::List::Iterator eventIt = events.begin();
            for ( ; eventIt != events.end(); ++eventIt ) {
                calSyncee->addEntry ( new KSync::CalendarSyncEntry ( ( *eventIt ) ->clone(), calSyncee ) );
            }

            KCal::Todo::List todos = cal.todos();
            KCal::Todo::List::Iterator todoIt = todos.begin();
            for ( ; todoIt != todos.end(); ++todoIt ) {
                calSyncee->addEntry ( new KSync::CalendarSyncEntry ( ( *todoIt ) ->clone(), calSyncee ) );
            }

            calFile.close();
        }
    }


    void PocketPCKonnector::saveAddressbookMetaData ( const QString& p_dir )
    {
        kdDebug( 2120 ) << "PocketPCKonnector: saveAddressbookMetaData" << endl;

        QString addrFileName = p_dir + "/AddressBookMeta.vcf";

        KSync::AddressBookSyncee* addrSyncee = m_syncees.addressBookSyncee();
        if ( addrSyncee->isValid() ) {
            QFile addrFile ( addrFileName );
            if ( addrFile.open( IO_WriteOnly ) ) {
                QTextStream addrStream ( &addrFile );
                KABC::VCardConverter conv;

                KSync::AddressBookSyncEntry* addrEntry = addrSyncee->firstEntry();
                while ( addrEntry ) {
                    kdDebug( 2120 ) << "     saving addr id: " << addrEntry->addressee().uid() << endl;
                    if ( addrEntry->state() != KSync::SyncEntry::Removed )  // removed entries dont need to be stored.. :)
                        addrStream << conv.createVCard ( addrEntry->addressee() ); // do we need a newline here? well.. test and see... :)
                    addrEntry = addrSyncee->nextEntry();
                }
                addrFile.close();
            }
        }
    }


    void PocketPCKonnector::saveCalendarMetaData ( const QString& p_dir )
    {

        kdDebug( 2120 ) << "PocketPCKonnector: saveCalendarMetaData" << endl;

        QString calFileName = p_dir + "/CalendarMeta.ics";

        KSync::CalendarSyncee* calSyncee = m_syncees.calendarSyncee();
        if ( calSyncee->isValid() ) {
            QFile calFile ( calFileName );
            if ( calFile.open ( IO_WriteOnly ) ) {
                QTextStream calStream ( &calFile );
                KCal::ICalFormat conv;

                QString vCalBegin = "BEGIN:VCALENDAR\nPRODID:-//K Desktop Environment//NONSGML KOrganizer 3.2.1//EN\nVERSION:2.0\n";
                QString vCalEnd = "END:VCALENDAR\n";

                KSync::CalendarSyncEntry* calEntry = calSyncee->firstEntry();

                calStream << vCalBegin;

                while ( calEntry ) {
                    kdDebug( 2120 ) << "      saving cal id: " << calEntry->incidence() ->uid() << endl;
                    if ( calEntry->state() != KSync::SyncEntry::Removed )
                        calStream << conv.toString ( calEntry->incidence() ); // maybe should use conv.toICalString!!! (and in CalendarHandler too.. possibly..)
                    calEntry = calSyncee->nextEntry();
                }
                calStream << vCalEnd;
                calFile.close();
            }
        }
    }


    void PocketPCKonnector::updateAddressBookSyncee ( KABC::Addressee::List& p_added, KABC::Addressee::List& p_modified, QStringList& p_removed )
    {
        kdDebug( 2120 ) << "PocketPCKonnector::updateAddressBookSyncee begin" << endl;

        KABC::Addressee::List::Iterator it;

        KSync::AddressBookSyncee* syncee = m_syncees.addressBookSyncee();

        KSync::AddressBookSyncEntry* syncEntry = 0;

        // add new entries
        it = p_added.begin();
        for ( ; it != p_added.end(); ++it ) {
            syncEntry = new KSync::AddressBookSyncEntry ( *it, syncee );
            syncee->addEntry ( syncEntry );
            setSyncEntry ( syncEntry, KSync::SyncEntry::Added );
        }

        // replace modified entries
        it = p_modified.begin();
        for ( ; it != p_modified.end(); ++it ) {
            syncEntry = new KSync::AddressBookSyncEntry ( *it, syncee );
            syncee->replaceEntry ( syncee->findEntry ( ( *it ).uid() ), syncEntry );
            setSyncEntry ( syncEntry, KSync::SyncEntry::Modified );
        }

        // mark removed entries
        QStringList::Iterator it2;
        it2 = p_removed.begin();
        for ( ; it2 != p_removed.end(); ++it2 ) {
            KSync::SyncEntry* entry = syncee->findEntry( ( *it2 ) );
            setSyncEntry ( entry, KSync::SyncEntry::Removed );
        }
    }


    void PocketPCKonnector::updateCalendarSyncee ( KCal::Event::List& p_added, KCal::Event::List& p_modified, QStringList& p_removedIds )
    {
        KSync::CalendarSyncee* syncee = m_syncees.calendarSyncee();

        KSync::CalendarSyncEntry* syncEntry = 0;

        KCal::Event::List::Iterator it;

        // add new events
        it = p_added.begin();
        for ( ; it != p_added.end(); ++it ) {
            syncEntry = new KSync::CalendarSyncEntry ( ( *it ) ->clone(), syncee );
            syncee->addEntry ( syncEntry );
            setSyncEntry ( syncEntry, KSync::SyncEntry::Added );
        }

        // replace modified events
        kdDebug( 2120 ) << "updateCalendarSyncee: modified:" << endl;
        it = p_modified.begin();
        for ( ; it != p_modified.end(); ++it ) {
            kdDebug( 2120 ) << "updateCalendarSyncee: modified: " << ( *it ) ->uid() << endl;
            syncEntry = new KSync::CalendarSyncEntry( ( *it ) ->clone(), syncee );
            syncee->replaceEntry ( syncee->findEntry ( ( *it ) ->uid() ), syncEntry );
            setSyncEntry ( syncEntry, KSync::SyncEntry::Modified );
        }

        // mark removed entries
        QStringList::Iterator it2 = p_removedIds.begin();
        for ( ; it2 != p_removedIds.end(); ++it2 ) {
            KSync::SyncEntry* entry = syncee->findEntry( ( *it2 ) );
            setSyncEntry ( entry, KSync::SyncEntry::Removed );
        }
    }


    void PocketPCKonnector::updateCalendarSyncee ( KCal::Todo::List& p_added, KCal::Todo::List& p_modified, QStringList& p_removedIds )
    {
        KSync::CalendarSyncee* syncee = m_syncees.calendarSyncee();

        KSync::CalendarSyncEntry* syncEntry = 0;

        KCal::Todo::List::Iterator ti;

        // add new todos
        ti = p_added.begin();
        for ( ; ti != p_added.end(); ++ti ) {
            syncEntry = new KSync::CalendarSyncEntry ( ( *ti ) ->clone(), syncee );
            syncee->addEntry ( syncEntry );
            setSyncEntry ( syncEntry, KSync::SyncEntry::Added );
        }

        // replace modified todos
        kdDebug( 2120 ) << "updateCalendarSyncee: modified:" << endl;
        ti = p_modified.begin();
        for ( ; ti != p_modified.end(); ++ti ) {
            kdDebug( 2120 ) << "updateCalendarSyncee: modified: " << ( *ti ) ->uid() << endl;
            syncEntry = new KSync::CalendarSyncEntry ( ( *ti ) ->clone(), syncee );
            syncee->replaceEntry ( syncee->findEntry ( ( *ti ) ->uid() ), syncEntry );
            setSyncEntry ( syncEntry, KSync::SyncEntry::Modified );
        }

        // mark removed entries
        QStringList::Iterator it2 = p_removedIds.begin();
        for ( ; it2 != p_removedIds.end(); ++it2 ) {
            KSync::SyncEntry* entry = syncee->findEntry( ( *it2 ) );
            setSyncEntry ( entry, KSync::SyncEntry::Removed );
        }
    }
};
