import { useState, useEffect } from "react";
import {
  DiscordSDK,
  Events as DiscordEvents,
  type Types as DiscordTypes,
} from "@discord/embedded-app-sdk";

const discordSdk = new DiscordSDK(import.meta.env.VITE_CLIENT_ID);

type User = {
  username: string;
  discriminator: string;
  id: string;
  public_flags: number;
  avatar?: string | null | undefined;
  global_name?: string | null | undefined;
};

function Participants({
  participants,
}: {
  participants: DiscordTypes.GetActivityInstanceConnectedParticipantsResponse;
}) {
  return (
    <div>
      {participants.participants.map((participant) => (
        <div key={participant.id} style={{
          display: "flex",
          gap: "1rem",
          alignItems: "center",
        }}>
          {participant.avatar && (
            <img
              src={`https://cdn.discordapp.com/avatars/${participant.id}/${participant.avatar}.png?size=64`}
              alt={participant.username}
              style={{
                width: "4rem",
                height: "4rem",
                borderRadius: "1rem",
              }}
            />
          )}
          <span>{participant.username}</span>
        </div>
      ))}
    </div>
  );
}

function App() {
  const [user, setUser] = useState<User | null>(null);
  const [participants, setParticipants] =
    useState<DiscordTypes.GetActivityInstanceConnectedParticipantsResponse | null>(
      null
    );

  useEffect(() => {
    async function setupDiscordSDK() {
      await discordSdk.ready();
      const { code } = await discordSdk.commands.authorize({
        client_id: import.meta.env.VITE_CLIENT_ID,
        response_type: "code",
        state: "",
        prompt: "none",
        scope: ["identify"],
      });

      await fetch("/.proxy/api/token", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          code,
        }),
      }).then(async (res) => {
        if (!res.ok) {
          throw new Error(res.statusText);
        }

        const { access_token } = (await res.json()) as { access_token: string };

        const auth = await discordSdk.commands.authenticate({
          access_token,
        });

        setUser(auth.user);
      });

      function updateParticipants(
        participants: DiscordTypes.GetActivityInstanceConnectedParticipantsResponse
      ) {
        setParticipants(participants);
      }

      discordSdk.subscribe(
        DiscordEvents.ACTIVITY_INSTANCE_PARTICIPANTS_UPDATE,
        updateParticipants
      );
    }

    setupDiscordSDK();
  }, []);

  return (
    <main
      style={{
        display: "flex",
        flexDirection: "column",
        gap: "1rem",
      }}
    >
      <section>
        <h1>Discord Activity</h1>
        <p>Instance ID: {discordSdk.instanceId}</p>
      </section>
      <section>
        <h2>Auth</h2>
        {user === null ? (
          <p>Authenticating...</p>
        ) : (
          <div>Hello, {user.username}!</div>
        )}
      </section>
      <section>
        <h2>Participants</h2>
        {participants === null ? (
          <p>Fetching participants...</p>
        ) : (
          <Participants participants={participants} />
        )}
      </section>
    </main>
  );
}

export default App;
