/*     -*-C++-*- -*-coding: utf-8-unix;-*-
    Classified Ads is Copyright (c) Antti Järvinen 2013.

    This file is part of Classified Ads.

    Classified Ads is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    Classified Ads is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with Classified Ads; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/
#ifdef WIN32
#define NOMINMAX 
#include <winsock2.h>
#else
#include "arpa/inet.h"
#endif
#include "protocol_message_parser.h"
#include "../log.h"
#include "../datamodel/mmodelprotocolinterface.h"
#include "../datamodel/mnodemodelprotocolinterface.h"
#include "../mcontroller.h"
#include <QDateTime>
#ifdef WIN32
#include <QJson/Serializer>
#include <QJson/Parser>
#else
#include <qjson/parser.h>
#include <qjson/serializer.h>
#endif
#include "../util/hash.h"
#include "../datamodel/profilemodel.h"
#include "../datamodel/binaryfilemodel.h"
#include "../datamodel/camodel.h"
#include "../datamodel/privmsgmodel.h"
#include "../datamodel/profilecommentmodel.h"
#include "../datamodel/searchmodel.h"
#include "protocol_message_formatter.h"

ProtocolMessageParser::ProtocolMessageParser(MController &aController,
    MModelProtocolInterface &aModel) :
  iController(aController),
  iModel(aModel) {

}

bool ProtocolMessageParser::parseMessage(const QByteArray& aGoodFood,
    Connection& aConnection) {
  LOG_STR2("::parseMessage bytes %d", aGoodFood.size()) ;
  if ( aGoodFood.size() < 4 ) {
    return false ; // stupid check but but packet is obviously a bit short.
  }
  const unsigned char protocol_item_type = aGoodFood.at(0) ;
  switch ( protocol_item_type ) {
  case KProtocolVersion:
    // node greeting with protocol version 1
    return parseMultipleNodeGreetingsV1(aGoodFood,aConnection ) ;
    break ;
  case KRandomNumbersPacket:
    LOG_STR2("Received random bytes %d", (int) aGoodFood.size()) ;
    // no need to parse further
    break ;
  case KNodesAroundHash:
  case KProfileAtHash:
  case KBinaryFileAtHash:
  case KClassifiedAdAtHash:
  case KPrivMessagesAtHash:
  case KPrivMessagesForProfile:
  case KProfileCommentAtHash: 
  case KSingleProfileCommentAtHash:
    // peer wants node-refs around hash
    return parseRequestForObjectsAroundHash(aGoodFood,aConnection ) ;
    break ;
  case KProfilePublish:
  case KProfileSend:
  case KBinaryFilePublish:
  case KBinaryFileSend:
  case KClassifiedAdPublish:
  case KClassifiedAdSend:
    // peer just published a profile/blob/ca
    return parseContentPublishedOrSent(protocol_item_type,aGoodFood,aConnection ) ;
    break ;
  case KPrivMessagePublish:
  case KPrivMessageSend:
    return parsePrivMsgPublishedOrSent(protocol_item_type,aGoodFood,aConnection ) ;
    break ;
  case KProfileCommentPublish:
  case KProfileCommentSend:
    return parseProfileCommentPublishedOrSent(protocol_item_type,aGoodFood,aConnection ) ;
    break ;
  case KAdsClassifiedAtHash:
    return parseAdsClassifiedAtHash(aGoodFood,aConnection ) ;
    break ; 
  case KListOfAdsClassifiedAtHash:
    return parseListOfAdsClassifiedAtHash(aGoodFood,aConnection ) ; 
    break ;    
  case KSearchRequest:
    return parseSearchRequest(aGoodFood,aConnection ) ; 
    break ; 
  case KSearchResults:
    return parseSearchResults(aGoodFood,aConnection ) ;
    break ; 
  case KFutureUse1:
  case KFutureUse2:
  case KFutureUse3:
  case KFutureUse4:
  case KFutureUse5:
  case KFutureUse6:
    LOG_STR2("Unhandled future-use item id %d", (int) protocol_item_type) ;
    break ; 
  default:
    // unknown message type
    LOG_STR2("Unhandled protocol item id %d", (int) protocol_item_type) ;
    break ;
  }
  return true ;
}

bool ProtocolMessageParser::parseContentPublishedOrSent(const unsigned char aProtocolItemType,
							const QByteArray& aPublishedContent,
							const Connection &aConnection ) {
  LOG_STR2("ProtocolMessageParser::parseContentPublished type %d", (int) aProtocolItemType) ;
  bool retval = false ; 
  // in publish there is 
  // 1. magick
  // 2. bangpath of low-order bits of hosts
  // 3. content hash
  // 4. len of data (32bit)
  // 5. data
  // 6. len of signature (32bit)
  // 7. signature
  // 8. length if siging key used (32bit)
  // 9. signing key 
  // 10. one unsigned char of flags, if bit 1 is on, 5. data is encrypted
  //                                 if bit 2 is on, 5. data is compressed with qCompress
  // 11. quint32 for timestamp (time_t) of the content
  if ( (unsigned)(aPublishedContent.size()) > 
       ( sizeof (char) + // magick
	 ( sizeof (quint32) * 5 ) + // bangpath
	 ( sizeof (quint32) * 5 ) + // hash
	 sizeof (quint32) + // content len
	 1 + // at least 1 byte of content 
	 sizeof (quint32) + // sig len
	 1 )) {
    // lets try
    int pos ( 0 ) ; 
    pos += sizeof(char) ; // skip magick
    quint32 bangpath1(0) ;
    quint32 bangpath2(0) ;
    quint32 bangpath3(0) ;
    quint32 bangpath4(0) ;
    quint32 bangpath5(0) ;

    // "send" items have no bangpath
    switch ( aProtocolItemType ) {
    case KBinaryFileSend:
    case KProfileSend:
    case KClassifiedAdSend:
      break ; 
    default: // and "Publish" items do have bangpath
      uintFromPosition(aPublishedContent, // because "send" has no bangpath
		       pos, 
		       &bangpath1) ;
      pos += sizeof(quint32) ; // skip first bangpath ; 
      uintFromPosition(aPublishedContent,
		       pos, 
		       &bangpath2) ;
      pos += sizeof(quint32) ; // skip 2nd bangpath ;
      uintFromPosition(aPublishedContent,
		       pos, 
		       &bangpath3) ;
      pos += sizeof(quint32) ; // skip 3rd bangpath ;
      uintFromPosition(aPublishedContent,
		       pos, 
		       &bangpath4) ;
      pos += sizeof(quint32) ; // skip 4th bangpath ;
      uintFromPosition(aPublishedContent,
		       pos, 
		       &bangpath5) ;
      pos += sizeof(quint32) ; // skip last bangpath ;
      break ; 
    }
    Hash* hashObtained = new Hash();
    if ( hashFromPosition(aPublishedContent,
			  pos,
			  hashObtained) == false ) {
      delete hashObtained  ;
      return false ;
    }
    pos += (sizeof(quint32) * 5) ; // skip hash
    quint32 contentlen(0) ; 
    uintFromPosition(aPublishedContent,
		     pos, 
		     &contentlen) ;
    pos += sizeof(quint32) ; // skip content len
    // content can't be  longer than 2 megabytes
    if ( ( contentlen > KMaxProtocolItemSize ) || ( (unsigned)(aPublishedContent.size()) < pos+contentlen ) ) {
      delete hashObtained  ;
      return false ;
    }
    QByteArray content ( aPublishedContent.mid(pos,contentlen) ) ;
    pos += contentlen ; 
    if ( aPublishedContent.size() < pos+2 ) {
      delete hashObtained  ;
      return false ;
    }
    quint32 signaturelen (0) ; 
    uintFromPosition(aPublishedContent,
		     pos, 
		     &signaturelen) ;
    pos += sizeof(quint32) ; // skip signature len
    if ( (unsigned)(aPublishedContent.size()) < pos+signaturelen+sizeof(quint32) ) {
      delete hashObtained  ;
      return false ;
    }
    QByteArray signature ( aPublishedContent.mid(pos,signaturelen) ) ;

    pos += signaturelen ; 
    // then extract signing key
    quint32 signingKeyLen (0) ; 
    uintFromPosition(aPublishedContent,
		     pos, 
		     &signingKeyLen) ;
    pos += sizeof(quint32) ; // skip signature len
    if ( (unsigned)(aPublishedContent.size()) < pos+signingKeyLen+sizeof(unsigned char) ) {
      delete hashObtained  ;
      return false ;
    }
    QByteArray signingKey ( aPublishedContent.mid(pos,signingKeyLen) ) ;
    pos += signingKeyLen ; 
    // pos should now point to flags.
    const unsigned char flags = aPublishedContent.at(pos) ;
    pos += sizeof(unsigned char) ; 
    // pos should now point to timestamp
    quint32 timestampOfContent (0) ;  
    uintFromPosition(aPublishedContent,
		     pos, 
		     &timestampOfContent) ;
#ifndef WIN32
#ifdef DEBUG
    time_t contentTime_T ( timestampOfContent ) ; 
    char timebuf[40] ; 
    LOG_STR2("Content time of publish at receive: %s", ctime_r(&contentTime_T,timebuf)) ;
#endif
#endif
    QList<quint32> bangPath ;

    switch ( aProtocolItemType ) {// skip bangpath for "send"
    case KBinaryFileSend:
    case KProfileSend:
    case KClassifiedAdSend:
      break ; 
    default: // and "Publish" items do have bangpath
      { 
	// here penalize hosts with hash last 32 bits all zero? 
	// they will still receive content via content-copy instead
	// of publish, and surely by asking..
	if ( bangpath1 > 0 ) {
	  bangPath.append(bangpath1) ; 
	}
	if ( bangpath2 > 0 ) { 
	  bangPath.append(bangpath2) ; 
	}
	if ( bangpath3 > 0 ) { 
	  bangPath.append(bangpath3) ; 
	}
	if ( bangpath4 > 0 ) {
	  bangPath.append(bangpath4) ; 
	}
	if ( bangpath5 > 0 ) {
	  bangPath.append(bangpath5) ; 
	}
	// if bangpath is not yet full, 
	if ( bangPath.size() < 5 ) {
	  // then append the host where we got this content:
	  iModel.lock() ;
	  bangPath.append(aConnection.node()->nodeFingerPrint().iHash160bits[4] ) ;
	  LOG_STR2("Appending to bangpath low-order bits %u", 
		   aConnection.node()->nodeFingerPrint().iHash160bits[4]) ; 
	  iModel.unlock() ;
	} 
      }
      break ;
    }


    // all right dudes.
    switch ( aProtocolItemType ) {
    case KProfilePublish:
      iModel.lock() ;
      retval = 
	iModel.profileModel().publishedProfileReceived(*hashObtained,
						       content,
						       signature,
						       bangPath,
						       signingKey,
						       flags,
						       timestampOfContent,
						       aConnection.node()->nodeFingerPrint()) ;
      iModel.unlock() ;
      break ; 
    case KProfileSend:
      iModel.lock() ;
      retval = 
	iModel.profileModel().sentProfileReceived(*hashObtained,
						  content,
						  signature,
						  signingKey,
						  flags,
						  timestampOfContent,
						  aConnection.node()->nodeFingerPrint()) ;
      LOG_STR2("Retval of profileModel().sentProfileReceived = %d", (int)retval) ; 
      iModel.unlock() ;      
      break ; 
    case KBinaryFileSend:
      iModel.lock() ;
      retval = 
	iModel.binaryFileModel().sentBinaryFileReceived(*hashObtained,
							content,
							signature,
							signingKey,
							flags,
							timestampOfContent,
							aConnection.node()->nodeFingerPrint()) ;
      iModel.unlock() ;
      break ; 
    case KClassifiedAdSend:
      iModel.lock() ;
      retval = 
	iModel.classifiedAdsModel().sentCAReceived(*hashObtained,
						   content,
						   signature,
						   signingKey,
						   flags,
						   timestampOfContent,
						   aConnection.node()->nodeFingerPrint()) ;
      iModel.unlock() ;
      break ; 
    case KBinaryFilePublish:
      iModel.lock() ;
      retval = 
	iModel.binaryFileModel().publishedBinaryFileReceived(*hashObtained,
							     content,
							     signature,
							     bangPath,
							     signingKey,
							     flags,
							     timestampOfContent,
							     aConnection.node()->nodeFingerPrint() ) ;
      iModel.unlock() ;
      break ; 
    case KClassifiedAdPublish:
      iModel.lock() ;
      retval = 
	iModel.classifiedAdsModel().publishedCAReceived(*hashObtained,
							content,
							signature,
							bangPath,
							signingKey,
							flags,
							timestampOfContent,
							aConnection.node()->nodeFingerPrint()) ;
      iModel.unlock() ;
      break ; 
    default:
      LOG_STR2("Unhandled publish item type %d", aProtocolItemType) ; 
      break ; 
    }
    delete hashObtained ; 
  }
  return retval ; 
}


bool ProtocolMessageParser::parsePrivMsgPublishedOrSent(const unsigned char aItemType,
							const QByteArray& aPublishedContent,
							const Connection &aConnection ) {
  LOG_STR2("ProtocolMessageParser::parsePrivMsgPublishedOrSent type %d", (int)aItemType);
  bool retval = false ; 
  // in publish of priv msg there is 
  // 1. magick numbah KPrivMessagePublish
  // 2. bangpath of low-order bits of hosts
  // 3. content hash
  // 4. len of data (32bit)
  // 5. data
  // 6. len of signature (32bit)
  // 7. signature
  // 8. quint32 for timestamp (time_t) of the content
  // 9. destination node hash (may be all zeroes and that's all right)
  // 10. recipient profile hash

  // if it is question of private message send, the structure is same, but 
  // bangpath is missing from between
  if ( (unsigned)(aPublishedContent.size()) > 
       ( sizeof (char) + // magick
	 ( sizeof (quint32) * 5 ) + // bangpath
	 ( sizeof (quint32) * 5 ) + // hash
	 sizeof (quint32) + // content len
	 1 + // at least 1 byte of content 
	 sizeof (quint32) + // sig len
	 1 )) {
    // lets try
    int pos ( 0 ) ; 
    pos += sizeof(char) ; // skip magick

    quint32 bangpath1(0) ;//these variables un-used in case of msg send
    quint32 bangpath2(0) ;
    quint32 bangpath3(0) ;
    quint32 bangpath4(0) ;
    quint32 bangpath5(0) ;

    if ( aItemType == KPrivMessagePublish ) {
      // publish has bangpath, send has not

      // read bangpath values from content
      uintFromPosition(aPublishedContent, 
		       pos, 
		       &bangpath1) ;
      pos += sizeof(quint32) ; // skip first bangpath ; 
      uintFromPosition(aPublishedContent,
		       pos, 
		       &bangpath2) ;
      pos += sizeof(quint32) ; // skip 2nd bangpath ;
      uintFromPosition(aPublishedContent,
		       pos, 
		       &bangpath3) ;
      pos += sizeof(quint32) ; // skip 3rd bangpath ;
      uintFromPosition(aPublishedContent,
		       pos, 
		       &bangpath4) ;
      pos += sizeof(quint32) ; // skip 4th bangpath ;
      uintFromPosition(aPublishedContent,
		       pos, 
		       &bangpath5) ;
      pos += sizeof(quint32) ; // skip last bangpath ;
    }
    // then try find content hash
    Hash* hashObtained = new Hash() ;
    if ( hashFromPosition(aPublishedContent,
			  pos,
			  hashObtained) == false ) {
      delete hashObtained ; 
      return false ;
    }
    pos += (sizeof(quint32) * 5) ; // skip hash
    // then read content len and content
    quint32 contentlen (0); 
    uintFromPosition(aPublishedContent,
		     pos, 
		     &contentlen) ;
    pos += sizeof(quint32) ; // skip content len
    // content can't be  longer than 2 megabytes
    if ( ( contentlen > KMaxProtocolItemSize ) || ( (unsigned)(aPublishedContent.size()) < pos+contentlen ) ) {
      delete hashObtained ; 
      return false ;
    }
    QByteArray content ( aPublishedContent.mid(pos,contentlen) ) ;
    pos += contentlen ; 
    if ( aPublishedContent.size() < pos+2 ) {
      delete hashObtained ; 
      return false ;
    }
    // then read signature len and signature
    quint32 signaturelen (0); 
    uintFromPosition(aPublishedContent,
		     pos, 
		     &signaturelen) ;
    pos += sizeof(quint32) ; // skip signature len
    if ( (unsigned)(aPublishedContent.size()) < pos+signaturelen+sizeof(quint32) ) {
      delete hashObtained ; 
      return false ;
    }
    QByteArray signature ( aPublishedContent.mid(pos,signaturelen) ) ;

    pos += signaturelen ; 



    // pos should now point to timestamp
    quint32 timestampOfContent (0) ;  
    uintFromPosition(aPublishedContent,
		     pos, 
		     &timestampOfContent) ;
#ifndef WIN32
#ifdef DEBUG
    time_t contentTime_T ( timestampOfContent ) ; 
    char timebuf[40] ; 
    LOG_STR2("Priv msg time of publish at receive: %s", ctime_r(&contentTime_T,timebuf)) ;
#endif
#endif
    pos += sizeof(quint32) ; // skip over timestamp
    Hash destinationNodeHash ;
    if ( hashFromPosition(aPublishedContent,
			  pos,
			  &destinationNodeHash) == false ) {
      delete hashObtained ; 
      return false ;
    }
    pos += sizeof(quint32)*Hash::KNumberOfIntsInHash ; // skip over dest hash
    Hash recipientHash ;
    if ( hashFromPosition(aPublishedContent,
			  pos,
			  &recipientHash) == false ) {
      delete hashObtained ; 
      return false ;
    }
    pos += sizeof(quint32)*Hash::KNumberOfIntsInHash ; // skip over recipient

    // post data into datamodel differently, depending on publish/send
    if ( aItemType == KPrivMessagePublish ) {
      QList<quint32> bangPath ;

      // here penalize hosts with hash last 32 bits all zero? 
      // they will still receive content via content-copy instead
      // of publish, and surely by asking..
      if ( bangpath1 > 0 ) {
	bangPath.append(bangpath1) ; 
      }
      if ( bangpath2 > 0 ) { 
	bangPath.append(bangpath2) ; 
      }
      if ( bangpath3 > 0 ) { 
	bangPath.append(bangpath3) ; 
      }
      if ( bangpath4 > 0 ) {
	bangPath.append(bangpath4) ; 
      }
      if ( bangpath5 > 0 ) {
	bangPath.append(bangpath5) ; 
      }
      // if bangpath is not yet full, 
      if ( bangPath.size() < 5 ) {
	// then append the host where we got this content:
	iModel.lock() ;
	bangPath.append(aConnection.node()->nodeFingerPrint().iHash160bits[4] ) ;
	LOG_STR2("Appending to bangpath low-order bits %u", 
		 aConnection.node()->nodeFingerPrint().iHash160bits[4]) ; 
	iModel.unlock() ;
      } 

      // all right dudes.

      iModel.lock() ;
      retval = 
	iModel.privateMessageModel().publishedPrivMessageReceived(*hashObtained,
								  content,
								  signature,
								  bangPath,
								  destinationNodeHash,
								  recipientHash,
								  timestampOfContent,
								  aConnection.node()->nodeFingerPrint()) ;
      iModel.unlock() ;
    } else {
      // was send
      iModel.lock() ;
      retval = 
	iModel.privateMessageModel().sentPrivMessageReceived(*hashObtained,
							     content,
							     signature,
							     destinationNodeHash,
							     recipientHash,
							     timestampOfContent,
							     aConnection.node()->nodeFingerPrint()) ;
      iModel.unlock() ;
    }
    delete hashObtained ; 
  }
  return retval ; 
}


bool ProtocolMessageParser::parseProfileCommentPublishedOrSent(const unsigned char aProtocolItemType,
							       const QByteArray& aPublishedContent,
							       const Connection &aConnection ){
  bool retval ( false ) ;
  // first magick number KProfileCommentPublish/KProfileCommentSend 
  // if ( aPublish ) {
  //  then bangpath 5 items and if not all 5 are filled,
  //  the remaining ones are filled with zeroes. 
  // }
  // then content hash, 5*32 bytes
  // then commented profile hash, 5*32 bytes

  // then length of the actual data, followed by the actual data
  // then length of the signature, followeds by the signature.
  // then flags, one unsigned 32-bit. 
  //  * if bit 1 is up, it means that content is encrypted.
  // then time when content was published. 

  int pos ( sizeof(unsigned char) ) ; // skip magick number, we have it in   
                                      // aProtocolItemType

  quint32 bangpath1(0) ;//these variables un-used in case of comment send
  quint32 bangpath2(0) ;
  quint32 bangpath3(0) ;
  quint32 bangpath4(0) ;
  quint32 bangpath5(0) ;

  if ( aProtocolItemType == KProfileCommentPublish ) {
    // publish has bangpath, send has not

    // read bangpath values from content
    if (!uintFromPosition(aPublishedContent, 
			  pos, 
			  &bangpath1) ) return false ; 
    pos += sizeof(quint32) ; // skip first bangpath ; 
    if (!uintFromPosition(aPublishedContent,
			  pos, 
			  &bangpath2) )  return false ; 
    pos += sizeof(quint32) ; // skip 2nd bangpath ;
    if (!uintFromPosition(aPublishedContent,
			  pos, 
			  &bangpath3) )  return false ; 
    pos += sizeof(quint32) ; // skip 3rd bangpath ;
    if (!uintFromPosition(aPublishedContent,
			  pos, 
			  &bangpath4) )  return false ; 
    pos += sizeof(quint32) ; // skip 4th bangpath ;
    if (!uintFromPosition(aPublishedContent,
			  pos, 
			  &bangpath5) )  return false ; 
    pos += sizeof(quint32) ; // skip last bangpath ;
  }

  // then comment hash
  Hash hashOfContent ; 
  if ( hashFromPosition(aPublishedContent,
			pos,
			&hashOfContent) == false ) {
    return false ;
  } else {
    pos += sizeof(quint32)*Hash::KNumberOfIntsInHash ; // skip hash
  }
  Hash hashOfProfileCommented ; 
  if ( hashFromPosition(aPublishedContent,
			pos,
			&hashOfProfileCommented) == false ) {
    return false ;
  } else {
    pos += sizeof(quint32)*Hash::KNumberOfIntsInHash ; // skip hash
  }

  quint32 contentlen (0); 
  uintFromPosition(aPublishedContent,
		   pos, 
		   &contentlen) ;
  pos += sizeof(quint32) ; // skip content len
  // content can't be  longer than 2 megabytes
  if ( ( contentlen > KMaxProtocolItemSize ) || ( (unsigned)(aPublishedContent.size()) < pos+contentlen ) ) {
    return false ;
  }
  const QByteArray content ( aPublishedContent.mid(pos,contentlen) ) ;
  pos += contentlen ; 
  quint32 signaturelen (0); 
  uintFromPosition(aPublishedContent,
		   pos, 
		   &signaturelen) ;

  if ( signaturelen > 5000 || ( (unsigned)(aPublishedContent.size()) < pos+signaturelen+sizeof(quint32) ) ) {
    return false ;
  } else {
    pos += sizeof(quint32) ; // skip signature len
  }
  const QByteArray signature ( aPublishedContent.mid(pos,signaturelen) ) ;
  pos += signaturelen ; 

  // ok, now we have content hash,profile hash,
  // content, signature, next item is flags:
  quint32 flags ; 
  if ( uintFromPosition(aPublishedContent,
			pos, 
			&flags) == false ) {
    return false ; 
  } else {
    pos += sizeof(quint32) ; // skip signature len
  }
  // and finally timestamp of publish:
  quint32 timeOfPublish ; 
  if ( uintFromPosition(aPublishedContent,
			pos, 
			&timeOfPublish) == false ) {
    return false ; 
  } else {
    if ( timeOfPublish > ( QDateTime::currentDateTimeUtc().toTime_t() + 600 ) ) {
      QLOG_STR("Clock skew detected, skipping content..") ; 
    } else {
      if ( aProtocolItemType == KProfileCommentPublish ) {
	iModel.lock() ;
	QList<quint32> bangPath ;

	// here penalize hosts with hash last 32 bits all zero? 
	// they will still receive content via content-copy instead
	// of publish, and surely by asking..
	if ( bangpath1 > 0 ) {
	  bangPath.append(bangpath1) ; 
	}
	if ( bangpath2 > 0 ) { 
	  bangPath.append(bangpath2) ; 
	}
	if ( bangpath3 > 0 ) { 
	  bangPath.append(bangpath3) ; 
	}
	if ( bangpath4 > 0 ) {
	  bangPath.append(bangpath4) ; 
	}
	if ( bangpath5 > 0 ) {
	  bangPath.append(bangpath5) ; 
	}
	// if bangpath is not yet full, 
	if ( bangPath.size() < 5 ) {
	  // then append the host where we got this content:
	  bangPath.append(aConnection.node()->nodeFingerPrint().iHash160bits[4] ) ;
	  LOG_STR2("Appending to bangpath low-order bits %u", 
		   aConnection.node()->nodeFingerPrint().iHash160bits[4]) ; 
	} 
	retval = 
	  iModel.profileCommentModel().publishedProfileCommentReceived(hashOfContent,
								       hashOfProfileCommented,
								       content,
								       signature,
								       bangPath,
								       flags,
								       timeOfPublish,
								       aConnection.node()->nodeFingerPrint()) ;
	iModel.unlock() ;
      } else {
	iModel.lock() ;
	retval = 
	  iModel.profileCommentModel().sentProfileCommentReceived(hashOfContent,
								  hashOfProfileCommented,
								  content,
								  signature,
								  flags,
								  timeOfPublish,
								  aConnection.node()->nodeFingerPrint()) ;
	iModel.unlock() ;
      }
    }
    // post the data into datamodel
  }
  return retval ; 
}

bool ProtocolMessageParser::parseMultipleNodeGreetingsV1(const QByteArray& aGoodFood,
    const Connection &aConnection ) {
  int pos  = 0 ;
  quint32 messageLen (0);
  bool retval = false ;
  LOG_STR2("parseMultipleNodeGreetingsV1 size = %d", aGoodFood.size()) ;
  while ( pos < aGoodFood.size() ) {
    LOG_STR2("parsemultiple pos = %d",(int)pos) ;
    LOG_STR2("parsemultiple total = %d",(int)aGoodFood.size()) ;
    if ( uintFromPosition( aGoodFood,
                           pos+1,
                           &messageLen) == true && messageLen > 0 ) {

      LOG_STR2("size of single msg = %d",(int)messageLen) ;
      if ( messageLen > (unsigned)(aGoodFood.size()) ) {
	return false ; // can not be
      }
      retval = parseNodeGreetingV1(aGoodFood.mid(pos,messageLen),aConnection) ;
      if ( !retval ) {
        break ;
      }
      pos = pos + messageLen ; 
    }
  }
  return retval ;
}
bool ProtocolMessageParser::parseNodeGreetingV1(const QByteArray& aSingleNodeGreeting,
    const Connection& aConnection ) {
  bool retval = false ;
  // byte 0 message identifier
  // byte 1-5 message length, quint32 in network byteorder
  // followed by JSON
  const unsigned char msgId = aSingleNodeGreeting[0] ;
  if (msgId == KProtocolVersion ) {
    quint32 JSONlen ;
    if ( uintFromPosition( aSingleNodeGreeting,
                           1,
                           &JSONlen) == true ) {
      if ( (int)(JSONlen ) == aSingleNodeGreeting.size() ) {
        QJson::Parser parser;
        bool ok;
        QByteArray unCompressedNodeGreeting ( qUncompress(aSingleNodeGreeting.mid(5))) ;
        if ( unCompressedNodeGreeting.size() > 0 ) {
          QVariantMap result = parser.parse (unCompressedNodeGreeting, &ok).toMap();
          LOG_STR2("Size of uncompressed = %d", unCompressedNodeGreeting.length()) ;
          if (!ok) {
            return false ;
          } else {

            Node* n ( Node::fromQVariant(result,
                                         aConnection.iNumberOfPacketsReceived == 1) ) ;
            if ( n ) {
              if ( (aConnection.iNumberOfPacketsReceived == 1) &&
                   (!(aConnection.node()->nodeFingerPrint() == n->nodeFingerPrint())) ) {
                LOG_STR("Node is sending different FP in greeting and SSL") ;
                delete n ;
                return false ;
              }
              iModel.lock() ;
	      if ( n->nodeFingerPrint() == iController.getNode().nodeFingerPrint() ) {
		QLOG_STR("Received my own node ref, not saving..") ; 
                retval = true ;
	      } else {
		bool wasInitialConnection ( aConnection.iNumberOfPacketsReceived == 1 ) ; 
		if (!iModel.nodeModel().nodeGreetingReceived(*n, 
							     wasInitialConnection ) ) {
		  LOG_STR("Update was not suksee") ;
		  retval = false ;
		} else {
		  LOG_STR2("Update was success, retval = %d", (int)retval) ;
		  if ( aConnection.iNumberOfPacketsReceived == 1 ) {
		    // node was updated/inserted ; datamodel has
		    // set possible last mutual connect time of
		    // that node into inserted node .. because connection
		    // holds different instance, copy the value there
		    aConnection.node()->setLastMutualConnectTime(n->lastMutualConnectTime()) ;
		    LOG_STR2("Setting last mutual of node to %u", (unsigned)(aConnection.node()->lastMutualConnectTime())) ; 
		  }
		  retval = true ;
		}
	      }
              iModel.unlock() ;
              delete n ;
            }
          }
        }
      }
    }
  }
  return retval ;
}

bool ProtocolMessageParser::uintFromPosition(const QByteArray& aGoodFood,
    const int aPos,
    quint32* aResult) const {
  if ( aGoodFood.size() < (aPos + (int)(sizeof(quint32)))) { // sizeof returns unsigned, size() signed ..
    return false ;
  }
  const char *byteArrayContent = aGoodFood.constData()  ;
  quint32* uintNetworkByteOrder = (quint32*)(&byteArrayContent[aPos]) ;
  *aResult = ntohl(*uintNetworkByteOrder) ;
  //  LOG_STR2("uint from pos %d ", aPos) ;
  //  LOG_STR2("is %x ", *aResult) ;
  return true;
}

bool ProtocolMessageParser::hashFromPosition(const QByteArray& aGoodFood,
    const int aPos,
    Hash* aResultHash) const {
  if ( aGoodFood.size() < (aPos + (int)(sizeof(quint32)*5))) { // sizeof returns unsigned, size() signed ..
    return false ;
  }

  quint32 hashBytes1 ;
  quint32 hashBytes2 ;
  quint32 hashBytes3 ;
  quint32 hashBytes4 ;
  quint32 hashBytes5 ;

  if ( uintFromPosition( aGoodFood,
                         aPos,
                         &hashBytes1) == false ) {
    LOG_STR2("hash bytes 1 not found at pos %d", aPos);
    return false ;
  }
  if ( uintFromPosition( aGoodFood,
                         aPos+sizeof(quint32),
                         &hashBytes2) == false ) {
    LOG_STR2("hash bytes 2 not found at pos %d", aPos);
    return false ;
  }
  if ( uintFromPosition( aGoodFood,
                         aPos+(sizeof(quint32)*2),
                         &hashBytes3) == false ) {
    LOG_STR2("hash bytes 3 not found at pos %d", aPos);
    return false ;
  }
  if ( uintFromPosition( aGoodFood,
                         aPos+(sizeof(quint32)*3),
                         &hashBytes4) == false ) {
    LOG_STR2("hash bytes 4 not found at pos %d", aPos);
    return false ;
  }
  if ( uintFromPosition( aGoodFood,
                         aPos+(sizeof(quint32)*4),
                         &hashBytes5) == false ) {
    LOG_STR2("hash bytes 5 not found at pos %d", aPos);
    return false ;
  }
  aResultHash->iHash160bits[0] = hashBytes1;
  aResultHash->iHash160bits[1] = hashBytes2;
  aResultHash->iHash160bits[2] = hashBytes3;
  aResultHash->iHash160bits[3] = hashBytes4;
  aResultHash->iHash160bits[4] = hashBytes5;

  return true;
}

bool ProtocolMessageParser::parseRequestForObjectsAroundHash(const QByteArray& aNodeRefRequest,
    const Connection &aConnection ) {
  if ((unsigned)(aNodeRefRequest.size()) < sizeof(unsigned char) +
      ( 5  * sizeof (quint32) ) ) {
    return false ;
  }
  Hash* hashObtained = new Hash() ;
  if ( hashFromPosition(aNodeRefRequest,
                        1,
                        hashObtained) == false ) {
    delete hashObtained ; 
    return false ;
  }
  quint32 timeStampOfMessages ( 0 ) ; 
  if ( aNodeRefRequest[0] == KPrivMessagesForProfile ||
       aNodeRefRequest[0] == KProfileCommentAtHash ) {
    // private messages + comment req contain an extra uint having the
    // timestamp of the messages
    if ( uintFromPosition( aNodeRefRequest,
			   1+(sizeof(quint32)*Hash::KNumberOfIntsInHash),
			   &timeStampOfMessages) == false ) {
      delete hashObtained ; 
      return false ;
    }
  }
  // ok, fine, now we have the hash and we know who requested it ;
  // who's gonna deliver?
  struct NetworkRequestExecutor::NetworkRequestQueueItem nodeRequest ;
  nodeRequest.iTimeStampOfItem = 0 ;
  switch ( aNodeRefRequest[0] ) {
  case KNodesAroundHash:
    // reply type for this query is series of nodegreetings
    nodeRequest.iRequestType = NodeGreeting ;
    nodeRequest.iMaxNumberOfItems = 10 ; 
    break ;
  case KProfileAtHash:
    // type of reply for "profile at hash" is surprisingly, profile.
    // for profilecomments there is separate call that has timestamp
    // along..
    nodeRequest.iRequestType = UserProfile ;
    nodeRequest.iMaxNumberOfItems = 1 ; 
    break ;
  case KBinaryFileAtHash:
    // type of reply for "profile at hash" is binary file
    nodeRequest.iRequestType = BinaryBlob ;
    nodeRequest.iMaxNumberOfItems = 1 ; 
    break ;
  case KClassifiedAdAtHash:
    // type of reply for "ca at hash" is classified a
    nodeRequest.iRequestType = ClassifiedAd ;
    nodeRequest.iMaxNumberOfItems = 1 ; 
    break ;
  case KPrivMessagesAtHash:
    nodeRequest.iRequestType = PrivateMessage ;
    nodeRequest.iMaxNumberOfItems = 1 ; 
    break ;    
  case KPrivMessagesForProfile:
    nodeRequest.iTimeStampOfItem = timeStampOfMessages  ; 
    nodeRequest.iRequestType = PrivateMessagesForProfile ;
    nodeRequest.iMaxNumberOfItems = 1 ; 
    break ;    
  case KProfileCommentAtHash:
    nodeRequest.iTimeStampOfItem = timeStampOfMessages  ; 
    // when network request executor sees this, it will queue profile comments
    // that comment given profile. those comments will then be sent individually.
    // if there has been update to the profile itself, 
    // it should be sent too
    nodeRequest.iRequestType = UserProfileCommentsForProfile ; 
    nodeRequest.iMaxNumberOfItems = 1000 ; 
    {
#ifndef WIN32
#ifdef DEBUG
    time_t commentTime_T ( timeStampOfMessages ) ; 
    char timebuf[40] ; 
    LOG_STR2("KProfileCommentAtHash for timestamp: %s", ctime_r(&commentTime_T,timebuf)) ;
#endif
#endif
    }
    break ; 
  case KSingleProfileCommentAtHash: // other nodes wants individual comment
    nodeRequest.iRequestType = UserProfileComment ;
    nodeRequest.iMaxNumberOfItems = 1 ; 
    break ; 
  default:
    LOG_STR2("Unhandled protocol request type %d", (int) (aNodeRefRequest[0]));
    delete hashObtained ; 
    return false ; // not handled
  }
  nodeRequest.iDestinationNode = aConnection.node()->nodeFingerPrint() ;
  nodeRequest.iRequestedItem = *hashObtained ;
  nodeRequest.iState = NetworkRequestExecutor::NewRequest ;
  iModel.lock() ;
  iModel.addNetworkRequest(nodeRequest) ;
  iModel.unlock() ;
  delete hashObtained ; 
  return true ;
}


bool ProtocolMessageParser::parseAdsClassifiedAtHash( const QByteArray& aQueryBytes,
						      const Connection &aConnection) 
{
  if (aQueryBytes.size() != sizeof(unsigned char) +
      ( 7  * sizeof (quint32) ) ) {
    // message contains identifier and a hash and 2 timestamps. so the size is fixed.
    return false ;
  } else {
    Hash* hashObtained = new Hash() ; 
    if ( hashFromPosition(aQueryBytes,
			  sizeof(unsigned char),
			  hashObtained) == false ) {
      delete hashObtained ; 
      return false ;
    }
    quint32 startTsHostBo ; 
    quint32 endTsHostBo ; 

    if ( uintFromPosition( aQueryBytes,
                           sizeof(unsigned char)+(sizeof(quint32)*5),
                           &startTsHostBo) == false ) {
      delete hashObtained ; 
      return false ;
    }
    if ( uintFromPosition( aQueryBytes,
                           sizeof(unsigned char)+(sizeof(quint32)*6),
                           &endTsHostBo) == false ) {
      delete hashObtained ; 
      return false ;
    }
    struct NetworkRequestExecutor::NetworkRequestQueueItem classifiedRequest ;
    classifiedRequest.iDestinationNode = aConnection.node()->nodeFingerPrint() ;
    classifiedRequest.iRequestedItem = *hashObtained ;
    classifiedRequest.iTimeStampOfItem = startTsHostBo ; 
    classifiedRequest.iTimeStampOfLastActivity = endTsHostBo  ;
    classifiedRequest.iState = NetworkRequestExecutor::NewRequest ;
    classifiedRequest.iRequestType = RequestAdsClassified ;
    iModel.lock() ; 
    iModel.addNetworkRequest(classifiedRequest) ;
    iModel.unlock() ;
    delete hashObtained ; 
    return true ;
  }
}

bool ProtocolMessageParser::parseListOfAdsClassifiedAtHash( const QByteArray& aQueryBytes,
							    const Connection &aConnection) 
{
  QList<QPair<Hash,quint32> > listOfAds ;
  int pos ( 1 ) ; // set to 1 to immediately skip the protocol identifier as it
                  // has been checked already 

  Hash hashOfClassification ;
  if ( hashFromPosition(aQueryBytes,
			pos,
			&hashOfClassification ) == false ) {
    return false ;
  }
  pos += sizeof(quint32)*Hash::KNumberOfIntsInHash ; // skip over hashOfClassication
  quint32 numberOfAdsInMessage ( 0 ) ; 
  if ( uintFromPosition( aQueryBytes,
			 pos, 
			 &numberOfAdsInMessage ) == false ) {
    return false ;
  }
  pos += sizeof(quint32) ; // skip over numberOfAdsInMessage

  for ( quint32 i = 0 ;  i < numberOfAdsInMessage ; i++ ) {
    Hash hashOfArticle ;
    quint32 timeStampOfArticle ; 
    if ( hashFromPosition(aQueryBytes,
			  pos,
			  &hashOfArticle ) == false ) {
      return false ;
    }
    pos += sizeof(quint32)*Hash::KNumberOfIntsInHash ; // skip over hash
    if ( uintFromPosition( aQueryBytes,
			   pos, 
			   &timeStampOfArticle ) == false ) {
      return false ;
    }    
    pos += sizeof(quint32) ; // skip over timestamp of article
    QPair<Hash,quint32> p ( hashOfArticle, timeStampOfArticle ) ;
    listOfAds.append(p) ;    


#ifndef WIN32
#ifdef DEBUG
    time_t articleTime_T ( timeStampOfArticle ) ; 
    char timebuf[40] ; 
    LOG_STR2("List of ads: Article timestamp: %s", ctime_r(&articleTime_T,timebuf)) ;
#endif
#endif
    QLOG_STR("List of ads " + hashOfArticle.toString() + " ts " + QString::number(timeStampOfArticle)) ;
  }
  return
	iModel.classifiedAdsModel().caListingByClassificationReceived(listOfAds,
								      aConnection.node()->nodeFingerPrint(),
								      hashOfClassification) ;

} 

bool  ProtocolMessageParser::parseSearchRequest( const QByteArray& aQueryBytes,
						 Connection& aConnection){
  bool retval = false ; 

  // ok, search request is
  // 8 bit magic
  // 32 bit flags
  // 32 bit search id to pass back in results
  // 32 bit length of search phrase
  // search phrase in utf-8

  int pos ( 1 ) ; // set to 1 to immediately skip the protocol identifier as it
  // has been checked already 

  unsigned len = (unsigned)(aQueryBytes.size()) ;
  if ( len > sizeof(unsigned char) + ( sizeof(quint32) * 3 ) ) {
    quint32 flags ( 0 ) ; 
    if ( uintFromPosition ( aQueryBytes,
			    pos,
			    &flags ) == false ) {
      return false ; 
    }
    pos +=  sizeof(quint32) ; 
    quint32 searchId ( 0 ) ; 
    if ( uintFromPosition ( aQueryBytes,
			    pos,
			    &searchId ) == false ) {
      return false ; 
    }
    pos +=  sizeof(quint32) ; 
    quint32 strLen ( 0 ) ; 
    if ( uintFromPosition ( aQueryBytes,
			    pos,
			    &strLen ) == false ) {
      return false ; 
    }
    pos +=  sizeof(quint32) ; 
    if ( len != ( strLen + ( sizeof(unsigned char) + ( sizeof(quint32) * 3 ) ) ) ) {
      return false ; 
    }
    QString queryStr ;
    queryStr = QString::fromUtf8(aQueryBytes.mid(pos,strLen).constData(), strLen) ; 
    if ( queryStr.length() > 0 ) {
      retval = true ; 
      bool searchAds = flags & 0x01 ; 
      bool searchProfiles = flags & ( 0x01 << 1 ) ;
      bool searchComments = flags & ( 0x01 << 2 ) ;
      iModel.lock() ; 
      QList<SearchModel::SearchResultItem> results = 
	iModel.searchModel()->performSearch(queryStr,
					    searchAds,
					    searchProfiles,
					    searchComments) ;
      if ( results.size() > 0 ) {
	// immediately queue for send
	QByteArray* serializedResults = new QByteArray() ;
	serializedResults->append( ProtocolMessageFormatter::searchResultsSend(results,
									       searchId)) ;
	if ( serializedResults->size() > 0 ) {
	  aConnection.iNextProtocolItemToSend.append(serializedResults) ;
	} else {
	  delete serializedResults ; 
	}
      }
      iModel.unlock() ; 
    }
  }
  return retval ; 
}

bool ProtocolMessageParser::parseSearchResults( const QByteArray& aQueryBytes,
						const Connection& aConnection) {
  bool retval = false ; 

  int pos ( 1 ) ; // set to 1 to immediately skip the protocol identifier as it
  // has been checked already 

  unsigned len = (unsigned)(aQueryBytes.size()) ;
  quint32 jsonLen ( 0 ) ; 
  if ( uintFromPosition ( aQueryBytes,
			  pos,
			  &jsonLen ) == false ) {
    return false ; 
  }
  pos += sizeof(quint32) ; 
  if ( len == (jsonLen + sizeof(unsigned char) + sizeof(quint32)) ) {
    QJson::Parser parser ;
    QVariantMap result = parser.parse ( aQueryBytes.mid(pos, jsonLen), &retval).toMap() ;
    if ( retval == true ) {
      QList<SearchModel::SearchResultItem> results;
      quint32 searchId ; 
      if ( SearchModel::deSerializeSearchResults(result,&results,&searchId) ) {
	iModel.lock() ; 
	iModel.searchModel()->appendNetworkSearchResults(results,
							 searchId,
							 aConnection.getPeerHash()) ;
	iModel.unlock() ; 
      } else {
	return false ; 
      }
    }
  } else {
    QLOG_STR("Search result protocol item size does not match!!") ;
  }
  return retval ; 
}
