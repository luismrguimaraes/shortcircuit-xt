/*
 * Shortcircuit XT - a Surge Synth Team product
 *
 * A fully featured creative sampler, available as a standalone
 * and plugin for multiple platforms.
 *
 * Copyright 2019 - 2023, Various authors, as described in the github
 * transaction log.
 *
 * ShortcircuitXT is released under the Gnu General Public Licence
 * V3 or later (GPL-3.0-or-later). The license is found in the file
 * "LICENSE" in the root of this repository or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * Individual sections of code which comprises ShortcircuitXT in this
 * repository may also be used under an MIT license. Please see the
 * section  "Licensing" in "README.md" for details.
 *
 * ShortcircuitXT is inspired by, and shares code with, the
 * commercial product Shortcircuit 1 and 2, released by VemberTech
 * in the mid 2000s. The code for Shortcircuit 2 was opensourced in
 * 2020 at the outset of this project.
 *
 * All source for ShortcircuitXT is available at
 * https://github.com/surge-synthesizer/shortcircuit-xt
 */

#ifndef SCXT_SRC_MESSAGING_MESSAGING_H
#define SCXT_SRC_MESSAGING_MESSAGING_H
#include "engine/engine.h"
#include "utils.h"

#include <thread>
#include <mutex>
#include <condition_variable>

#include <queue>
#include <stack>
#include <chrono>

#include "readerwriterqueue.h"
#include "client/client_serial.h"
#include "audio/audio_serial.h"

namespace scxt::messaging
{
/**
 * The SCXT Next messaging model
 *
 * We want SCXT "engine" to be able to provide a JSON interface at its edge to
 * a consumer, but obviously we don't want those messages on the audio thread (all
 * that string allocation!! all that parsing time) but also we don't want to require
 * a UI thread to run (what if we want a headless server mode, or an HTTP interface!)
 *
 * So our messaging strategy ends up with three thread
 *
 * Engine Audio Thread
 *    - Runs ::process
 *    - Can't allocate memory
 *    - Cant use anything other than lock free data structures
 *
 * Engine Serialization Thread
 *     - Runs its own ::run
 *     - Can allocate
 *     - Can do read-only access to Engine memrory (same process as the audio thread)
 *     - Can use system mutex/cond var etc.. to communicate with anything other than audio
 *
 * Client Thread (aka UI Thread)
 *      - Can do wahtever it wants except...
 *      - Talk to the engine audio thread or engine data structures (separate memory)
 *
 * So that results in the following data structures
 *
 * Audio <-> Serialization
 *    - A pair of lock free queues called "serializationToAudio" and "audioToSeralization"
 *    - Fixed data structures only in audioToSerialization (no alloc)
 *    - If serializatToAudio has allocated structues, the engine hands them back for future freeing
 *    - Engine has an atomic bool (or perhaps int) of client connected (or client count)
 *
 *    in Engine::process
 *       - drain serializationToAudio and respond. These will almost exclusively be
 *         state mutation operators
 *       - send updates back. These will be things like noteon-off display indicates,
 *         parameter change displays and so on. Only do this if a client is connected.
 *
 * Client <-> Serialization
 *    - A queue locked by a mutex probably
 *    - A condition variable for queue mutation notifications
 *    - A callback function to send a response to the client which can be called
 *      from the serialization thread. (In the out-of-process model this would be
 *      a socket write most likely).
 *
 * Serialization::run
 *    - basically wait on the condition variable if the queues are emtpy
 *    - then drain the audioToSerialization queue and generate the associated outbound
 *      messages by calling the ui callback (but don't hold the lock while doing this)
 *    - The serialization thread blocks for inbound from the UI and
 *      does a check of the outbound thread every 50ms or so. If you
 *      want a faster outbound process send an idle wakeup message
 *
 * We encapsulate this in a few objects here.
 *
 * The MessageController is an object owned by the engine, constructed with an
 * engine. This is the object which can send messages between the various parts.
 * Those messages take a couple of forms
 *
 * Client <-> Serialization Messages (clients/client_serial.h)
 *
 * The pattern we use here is to make request and response objects. Request
 * objects are C2S and response are S2C, basically. There is a unique set of
 * IDs for each message.
 *
 * To be a C2S object you implement an object which has a constecxpr id c2s_id
 * and a typedef to c2s_payload_t. That payload should be json streamable (so
 * should be a basic type supported by jsoncpp or implement a traint in src/json).
 * Finally the object should implement static executeOnSerialization which is called
 * with a payload, an engine, and a controller. A client sends a message with
 * template<T> messaging::client::clientSendToSerialization(T &msg, MessageController &)
 * which uses these template properties to serialize the payload, deliver it to the
 * seralization thread, and execute it.
 *
 * To be an S2C object you do the inverse. Implement a constexpr s2c_id
 * and typedef an s2c_payload_t which again is json streamable. Then implement
 * template<Client> executeOnClient(Client *, s2c_payload &). This allows the
 * serialization thread to call MessageController::serializationToClient(s2cid, payload)
 * and have that delivered to the client for execution. Obviously the union of the
 * functions called on the Client template type by executeOnClient imply an API for
 * the client object which unpacks messages.
 *
 * Audio <-> Serlization Messages (audio/audio_serial.h)
 *
 * The audio thread messaging is more constrained since it has to be non allocating
 * and lock free. As a result the message payloads are fixed sized unions
 * AudiotoSerialization and SerializationToAudio, which you enqueue or
 * dequeue with a type for a switch.
 *
 * For the specific case of the serialization thread interacting with
 * the audio thread, though, we have used this mechanism to make a
 * fucntion callback which is audio-thread alloc free. The function
 * MessageController::scheduleAudioThreadCallback(cb) hides most of these
 * mechanics by sending a function to the audio thread with all of the
 * required allocation and deallocation happening on the serialization
 * thread.
 *
 * The other common pattern of the audio thread initiates a message is
 * really just done by explicitly constructing the message on the audio
 * thread, sending it, and then unpacking it in
 * MessageController::praseAudioMessageOnSerializationThread
 *
 */

struct MessageController : MoveableOnly<MessageController>
{
  private:
    engine::Engine &engine;

  public:
    MessageController(engine::Engine &e) : engine(e) {}
    ~MessageController();

    /**
     * start. Called from the startup thread after the engine is created.
     * Will begin a serialization thread.
     */
    void start();

    /**
     * stop. Called from the startup thread when the engine is destroyed.
     * Will end and join the serialization thread. Cannot be called from
     * the serialization thread.
     */
    void stop();

    /**
     * The client callback is a function a client registers which will
     * get called on the serialization thread when there's a message
     * to send to the client. A typical implementation would be to lock
     * a queue which the client polls in an idle loop.
     */
    typedef std::string serialToClientMessage_t;
    typedef std::string clientToSerializationMessage_t;

    typedef std::function<void(const serialToClientMessage_t &)> clientCallback_t;
    clientCallback_t clientCallback{nullptr};

    /**
     * Register a client. Called from the client thread.
     *
     * @param nm The client name
     * @param f The client callback function
     */
    void registerClient(const std::string &nm, clientCallback_t &&f);

    /**
     * Unregister client. Called from the client thread.
     */
    void unregisterClient();

    /**
     * Allows the client to send a raw message on the client thread.
     * Used in the serialization implementation under the covers. Users
     * should use messaging::client::clientSendToSerialization instead
     * @param s
     */
    void sendRawFromClient(const clientToSerializationMessage_t &s);

    typedef audio::SerializationToAudio serializationToAudioMessage_t;
    typedef audio::AudioToSerialization audioToSerializationMessage_t;

    /**
     * Send a message to the serialization thread from the audio thread.
     * Call from the audio thread.
     *
     * @param m The message object
     */
    void sendAudioToSerialization(const audioToSerializationMessage_t &m)
    {
        audioToSerializationQueue.try_emplace(m);
    }

    /**
     * Send a message from the serialization thread to the audio thread.
     * Called from the serialization queue.
     *
     * @param m
     */
    void sendSerializationToAudio(const serializationToAudioMessage_t &m)
    {
        serializationToAudioQueue.try_emplace(m);
    }

    /**
     * Schedule a function on the audio thread from the serialization thread.
     * @param f
     */
    void scheduleAudioThreadCallback(std::function<void(engine::Engine &)> f);
    struct AudioThreadCallback
    {
      public:
        void setFunction(std::function<void(engine::Engine &)> &to) { f = std::move(to); }
        inline void exec(engine::Engine &e)
        {
            assert(e.getMessageController()->threadingChecker.isAudioThread());
            f(e);
        }

      private:
        std::function<void(engine::Engine &)> f;
    };

    // The engine has direct access to the audio queues
    friend class engine::Engine;

    /*
     * Various utilitues
     */
    ThreadingChecker threadingChecker;

  private:
    void runSerialization();
    void parseAudioMessageOnSerializationThread(const audio::AudioToSerialization &as);

    // serialization thread only please
    AudioThreadCallback *getAudioThreadCallback();
    void returnAudioThreadCallback(AudioThreadCallback *);
    std::stack<AudioThreadCallback *> cbStore;

    moodycamel::ReaderWriterQueue<serializationToAudioMessage_t, 1024> serializationToAudioQueue{
        128};
    moodycamel::ReaderWriterQueue<audioToSerializationMessage_t, 1024> audioToSerializationQueue{
        128};

    std::queue<clientToSerializationMessage_t> clientToSerializationQueue;
    std::mutex clientToSerializationMutex;
    std::condition_variable clientToSerializationConditionVar;

    int serializationToClientCallback;

    std::atomic<int> clientCount{0};
    std::atomic<bool> shouldRun{false};

    std::unique_ptr<std::thread> serializationThread;
};

} // namespace scxt::messaging

#include "messaging/client/detail/client_serial_impl.h"
#include "client/client_messages.h"

#endif // SHORTCIRCUIT_MESSAGING_H
