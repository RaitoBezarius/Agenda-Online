#include "Serveur.hpp"

Serveur::Serveur(int handle, QObject *parent) :
    QThread(parent), myHandle(handle), taillePacket(0), UserName(tr("Un client non authentifi�")), threadRunning(true), errorFatal(false)
{
}
Serveur::~Serveur()
{
    delete myClient;
}

void Serveur::run()
{
    myClient = new QTcpSocket;

    if(!myClient->setSocketDescriptor(myHandle))
    {
        emit message(tr("Error : When setting Socket Descriptor : %1").arg(QString::number(myHandle)));
        return;
    }

    while((threadRunning) || (!errorFatal))
    {
        QMutexLocker locker(&mutex);
        if(myClient->waitForReadyRead(5000))
        {
            emit message(tr("Donn�es re�ues depuis %1").arg(UserName));
            processData();
        }
        myClient->waitForBytesWritten();
        msleep(750);

        if(myClient->state() == QTcpSocket::UnconnectedState)
        {
            if(!UserName.startsWith("Un client"))
                emit removeClient(UserName, Classe);
            break;
        }
    }
}

void Serveur::processData()
{
    QDataStream in(myClient);

    if(taillePacket == 0)
    {
        if(myClient->bytesAvailable() < (int)sizeof(quint16))
            return;

        in >> taillePacket;
    }
    if(myClient->bytesAvailable() < taillePacket)
        return;

    quint8 header;
    in >> header;

    emit message(tr("Donn�es re�ues pour Header(%1) et pour taille(%2)").arg(QString::number(header), QString::number(taillePacket)));

    switch(header)
    {
    case CMSG_MESSAGE_LEGER:
    {
        QString messageRecu;
        in >> messageRecu;
        QString messageSecondRecu;
        in >> messageSecondRecu;

        QByteArray encryptedPassword;
        in >> encryptedPassword;

        emit message(tr("Message l�ger re�u depuis un client : %1\n%2\n%3").arg(messageRecu, messageSecondRecu, encryptedPassword.toHex()));
        break;
    }
    case CMSG_MESSAGE_CUSTOM:
    {
        /** Cr�er un comportement de test. **/
        break;
    }
    case CMSG_PING:
    {
        if(!UserName.startsWith("Un client"))
            emit message(tr("Ping re�u de %1.").arg(UserName));
        else
            emit message(tr("Ping re�u d'un client non authentifi�."));

        reponse(SMSG_PONG);
        break;
    }
    case CMSG_PONG:
    {
        if(!UserName.startsWith("Un client"))
            emit message(tr("Pong! Re�u de %1").arg(UserName));
        else
            emit message(tr("Pong! Re�u d'un client non authentifi�."));
        break;
    }
    case CMSG_MESSAGE_AUTH:
    {
        QString userName;
        in >> userName;

        QByteArray password;
        in >> password;

        if(SQLServerSupervisor::GetInstance()->Authentificate(userName, password))
        {
            UserName = userName;
            Classe = SQLServerSupervisor::GetInstance()->FindClasse(userName);

            emit message(tr("Tentative d'authentification de %1 r�ussi !").arg(userName));
            emit newClient(userName, Classe);

            reponse(SMSG_AUTHENTIFICATION_SUCCESS);
        }
        else
        {
            emit message(tr("Tentative d'authentification de %1 rat� !").arg(userName));
            reponse(SMSG_AUTHENTIFICATION_FAILED);
        }

        break;
    }
    case CMSG_MESSAGE_HOMEWORKFOR:
    {
        if(UserName.startsWith("Un client"))
        {
            reponse(SMSG_YOU_ARE_NOT_AUTHENTIFIED);
            emit message(tr("Demande de devoir de la part d'un client non authentifi� !! (Refus�)"));
            break;
        }
        QString matiere = tr("all");
        bool needOnlyMatiere;
        in >> needOnlyMatiere;

        if(needOnlyMatiere)
            in >> matiere;

        emit message(tr("Demande de devoir re�u de la part de %1 en classe de %2").arg(UserName, Classe));

        QList<Devoir> devoirs = SQLServerSupervisor::GetInstance()->LoadHomeworks(Classe, matiere);
        SendHomeworks(devoirs);
        break;
    }
    case CMSG_MESSAGE_CHAT:
    {
        if(UserName.startsWith("Un client"))
        {
            reponse(SMSG_YOU_ARE_NOT_AUTHENTIFIED);
            emit message(tr("Message envoy� d'un client inconnu."));
            break;
        }

        QString messageChat;
        in >> messageChat;

        emit message(tr("Message envoy� par Chat de la part de %1, contenu(%2)").arg(UserName, messageChat));

        if(ChatServer::GetInstance()->HasClient(myClient))
        {
            ChatServer::GetInstance()->SendMessageAt(UserName, messageChat, Classe);
        }
        else
        {
            ChatServer::GetInstance()->AddClient(myClient, UserName, Classe);
            ChatServer::GetInstance()->SendMessageAt(UserName, messageChat, Classe);
        }
        break;
    }
    case CMSG_MESSAGE_LISTMATIERE:
    {
        QStringList listMatiere = SQLServerSupervisor::GetInstance()->GetAllMatiereFromClasse(Classe);
        emit message(tr("Demande des mati�res disponibles pour la classe %1 de %2").arg(Classe, UserName));
        SendMatieres(listMatiere);

        break;
    }
    default:
    {
        emit message(tr("Header inconnu d�tect� : 0x%1").arg(QString::number(header, 16).toUpper()));
        break;
    }
    }

    taillePacket = 0;
}
void Serveur::SendHomeworks(const QList<Devoir> &devoirs)
{
    QByteArray paquet;
    QDataStream out(&paquet, QIODevice::WriteOnly);

    out << (quint16) 0;
    out << (quint8) SMSG_HOMEWORK;
    out << (quint32) devoirs.size();

    for(QListIterator<Devoir> it(devoirs) ; it.hasNext() ;)
    {
        Devoir devoir = it.next();
        out << devoir;
    }
    out.device()->seek(0);
    out << (quint16) (paquet.size() - sizeof(quint16));

    myClient->write(paquet);
}

void Serveur::reponse(quint8 rCode)
{
    QByteArray paquet;
    QDataStream out(&paquet, QIODevice::WriteOnly);

    out << (quint16) 0;
    out << (quint8) rCode;
    out.device()->seek(0);
    out << (quint16) (paquet.size() - sizeof(quint16));

    myClient->write(paquet);

    emit message(tr("Reponse sended with rCode = %1").arg(QString::number(rCode)));
}

void Serveur::SendPing()
{
    QByteArray paquet;
    QDataStream out(&paquet, QIODevice::WriteOnly);

    out << (quint16) 0;
    out << (quint8) SMSG_PING;
    out.device()->seek(0);
    out << (quint16) (paquet.size() - sizeof(quint16));

    myClient->write(paquet);

    emit message(tr("Ping sended !"));
}

void Serveur::SendMatieres(const QStringList &matieres)
{
    QByteArray paquet;
    QDataStream out(&paquet, QIODevice::WriteOnly);

    out << (quint16) 0;
    out << (quint8) SMSG_LISTMATIERE;
    out << matieres;
    out.device()->seek(0);
    out << (quint16) (paquet.size() - sizeof(quint16));

    myClient->write(paquet);

    emit message(tr("Mati�res demand� envoy�s."));
}