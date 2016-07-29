require File.expand_path("./spec_helper", File.dirname(__FILE__))

describe "HTTP Dynamic Redirect Module" do
  let(:origin) do
    "http://#{nginx_host}:#{nginx_port}/"
  end

  let(:dest) do
    "http://#{nginx_host}:#{nginx_port}0/"
  end

  before do
    insert_entry_on_redis(origin, dest)
  end

  context "disabled" do
    let(:config) do
      { dynamic_redirect_switch: "off" }
    end

    it "does not redirect" do
      nginx_run_server(config) do
        expect(response_for(origin).code).to eq '200'
      end
    end
  end

  context "enabled" do
    let(:config) do
      { dynamic_redirect_switch: "on" }
    end

    it "redirect" do
      nginx_run_server(config) do
        expect(response_for(origin).code).to eq '301'
      end
    end

    it "set location header" do
      nginx_run_server(config) do
        expect(response_for(origin)['location']).to eq dest
      end
    end

    it "return error if redis is offline" do
      nginx_run_server(config) do
        redis_stop
        sleep 0.1
        expect(response_for(origin).code).to eq '500'
        redis_start
      end
    end

    it "work if redis reconnect" do
      nginx_run_server(config) do
        redis_stop
        sleep 0.1
        expect(response_for(origin).code).to eq '500'

        redis_start
        sleep 0.1
        insert_entry_on_redis(origin, dest)

        expect(response_for(origin).code).to eq '301'
      end
    end
  end
end
